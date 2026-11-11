// The chapter 3 crash (runs 15-20): a Lua error escalates to the CRT exit (0x828A09F8 is the `li r3,1; b <exit>`
// wrapper the eight Lua panic paths 0x825ED7C0..0x825FDB60 call), and the teardown that follows crashes with the
// read of guest 0x6 the logs show. The retail build's error report goes nowhere we can see, so this hook first dumps
// what the panic left behind: the guest registers, the return addresses on the guest stack (the panic site and its
// callers), and every readable string the stack still points at (the Lua error message among them).
//
// Then, where the compiler left real code after the `bl` (it did not mark exit noreturn), the exit is skipped and
// that code — the game's own recovery path — runs instead: the game keeps going instead of dying at the chapter 3
// load. Where nothing safe follows the call (a zero word or the next function's prologue), the exit stands: falling
// through there would only crash differently.

#include <atomic>

#include <rex/hook.h>
#include <rex/memory.h>
#include <rex/system/kernel_state.h>

#if defined(DS1_HOOK_ds1_exit_wrapper)

namespace {

// Guest addresses in the image (code and read-only data): always mapped.
constexpr uint32_t kImagenIni = 0x82000000;
constexpr uint32_t kImagenFin = 0x82E80000;

// The `bl 0x828A09F8` sites whose following instruction is real code of the same function.
constexpr uint32_t kSitiosConRecuperacion[] = {
    0x825ED7C0,  // list pop continues
    0x825F42F8,  // error state is cleared, recovery setup
    0x825F496C,  // stores and a blr: a clean function tail
    0x825F5F34,  // the stack-grow path continues
};

bool legible(uint32_t direccion) {
  auto* memoria = REX_KERNEL_STATE()->memory();
  uint32_t proteccion = 0;
  auto* heap = memoria->LookupHeap(direccion);
  return heap && heap->QueryProtect(direccion, &proteccion) &&
         (proteccion & rex::memory::kMemoryProtectRead);
}

uint8_t* traducir(uint32_t direccion) { return REX_KERNEL_STATE()->memory()->TranslateVirtual(direccion); }

uint32_t leer32(uint32_t direccion) { return rex::memory::load_and_swap<uint32_t>(traducir(direccion)); }

std::string probarCadena(uint32_t direccion, size_t largoMax) {
  std::string salida;
  for (size_t i = 0; i < largoMax; ++i) {
    const uint8_t c = rex::memory::load_and_swap<uint8_t>(traducir(direccion + i));
    if (c == 0) break;
    if (c < 0x20 || c > 0x7E) return "";
    salida += char(c);
  }
  return salida.size() >= 4 ? salida : std::string();
}

std::atomic<uint32_t> g_veces{0};

}  // namespace

REX_EXTERN(__imp__ds1_lua_intern);
REX_EXTERN(ds1_lua_intern) {
  // The chapter 3 crash, third round: the file patch (0034) sees the bytes only while they travel
  // through a read, but the flicker script reaches the game inside a compressed bundle section and is
  // decompressed in guest memory after the read, with its "SetWatchValue" intact. Every Lua string is
  // interned after that (luaS_newlstr, (L, str, len) at 0x825FBC38), so here the tuner-only watch API
  // names - which this build of the game never registers, making any call on them a Lua panic that
  // escalates to exit - are rewritten to "GetSwarmCount", a registered query that ignores its
  // arguments. The length is ours to change: the interner takes it as an argument.
  if (ctx.r4.u32 != 0 && ctx.r5.u32 >= 8 && ctx.r5.u32 <= 32) {
    uint8_t* str = traducir(ctx.r4.u32);
    if (str && legible(ctx.r4.u32)) {
      // (name, replacement): a replacement never longer than the name it overwrites in place.
      static constexpr struct { const char* nombre; const char* reemplazo; } kNombres[] = {
          {"SetWatchValue", "GetSwarmCount"}, {"ClearWatchValue", "GetSwarmCount"},
          {"GetWatchValue", "GetSwarmCount"}, {"SetWatch", "type"},
          {"GetWatch", "type"},               {"ClearWatch", "type"},
      };
      for (const auto& par : kNombres) {
        const size_t n = std::strlen(par.nombre);
        if (ctx.r5.u32 == n && std::memcmp(str, par.nombre, n) == 0) {
          static std::atomic<uint32_t> veces{0};
          const uint32_t v = veces.fetch_add(1) + 1;
          if (v <= 10 || (v & (v - 1)) == 0) {
            REXLOG_WARN("[intern] «{}» -> «{}» (vez {})", par.nombre, par.reemplazo, v);
          }
          const size_t m = std::strlen(par.reemplazo);
          std::memcpy(str, par.reemplazo, m);
          ctx.r5.u64 = m;
          break;
        }
      }
    }
  }
  __imp__ds1_lua_intern(ctx, base);
}

REX_EXTERN(__imp__ds1_exit_wrapper);
REX_EXTERN(ds1_exit_wrapper) {
  const uint32_t n = g_veces.fetch_add(1) + 1;
  const uint32_t sitio = uint32_t(ctx.lr) - 4;  // the bl itself
  REXLOG_ERROR("[exit] salida del invitado (pánico de Lua) n.º {} desde {:08X}: lr {:08X} r1 {:08X} r3 {:08X} r13 "
               "{:08X} r30 {:08X} r31 {:08X}",
               n, sitio, uint32_t(ctx.lr), ctx.r1.u32, ctx.r3.u32, ctx.r13.u32, ctx.r30.u32, ctx.r31.u32);

  // Return addresses and string pointers still on the guest stack, from r1 upwards (the live frames).
  const uint32_t pila = ctx.r1.u32;
  int direcciones = 0, cadenas = 0;
  for (uint32_t sp = pila; sp < pila + 0x4000 && sp; sp += 4) {
    if (!legible(sp)) break;
    const uint32_t v = leer32(sp);
    if (kImagenIni <= v && v < kImagenFin && direcciones < 96) {
      ++direcciones;
      REXLOG_ERROR("[exit]   pila {:+5d}: {:08X}", int(sp - pila), v);
    } else if (0x82000000 <= v && v < 0xE0000000 && cadenas < 24) {
      if (!legible(v)) continue;
      const std::string s = probarCadena(v, 200);
      if (!s.empty()) {
        ++cadenas;
        REXLOG_ERROR("[exit]   pila {:+5d} -> {:08X}: «{}»", int(sp - pila), v, s);
      }
    }
  }
  if (direcciones == 0 && cadenas == 0) {
    REXLOG_ERROR("[exit]   (nada legible sobre r1)");
  }

  // The Lua state: luaD_throw kept it in r31 (0x825ED838 reads +0x50 from it). The error message is a string on
  // its stack ([L+8] .. [L+0x18]), pointed at by the stack slots; the strings sit inside the pointed-to objects
  // (Lua's TString header before the characters), so a few offsets are tried per pointer.
  const uint32_t L = ctx.r31.u32;
  if (legible(L)) {
    std::string cabecera;
    for (uint32_t a = L; a < L + 0x58; a += 4) {
      cabecera += fmt::format(" {:08X}", leer32(a));
    }
    REXLOG_ERROR("[exit]   lua_State {:08X}:{}", L, cabecera);
    const uint32_t tope = leer32(L + 8), fin = leer32(L + 0x18);
    if (tope >= 0x82000000 && fin > tope && fin - tope < 0x8000 && legible(tope) && legible(fin)) {
      REXLOG_ERROR("[exit]   pila Lua {:08X}..{:08X}:", tope, fin);
      int textos = 0;
      for (uint32_t p = tope; p < fin && p < tope + 0x400 && textos < 40; p += 4) {
        const uint32_t v = leer32(p);
        if (v < 0x82000000 || v >= 0xE0000000 || !legible(v)) continue;
        for (const uint32_t desfase : {0u, 4u, 8u, 0xCu, 0x10u}) {
          if (!legible(v + desfase)) continue;
          const std::string s = probarCadena(v + desfase, 200);
          if (s.size() >= 6) {
            ++textos;
            REXLOG_ERROR("[exit]     pilaLua {:+4d} -> {:08X}+{:X}: «{}»", int(p - tope), v, desfase, s);
            break;
          }
        }
      }
    }
  }

  for (const uint32_t seguro : kSitiosConRecuperacion) {
    if (sitio == seguro) {
      REXLOG_WARN("[exit]   salida saltada: el juego sigue por su propio camino de recuperación");
      ctx.r3.u64 = 0;
      return;
    }
  }

  // Last resort, for the sites with nothing safe after the call (0x825ED874 among them): unwind the
  // guest stack back to the protected-call wrapper (sub_82516488), right after its `bl 0x825F6B90`
  // (return address 0x82516554, seen on the stack of every one of these panics). The generated code
  // inlines guest functions, so the guest registers and the guest stack are what drive the flow from
  // here: with r1 at the wrapper's frame and r3 nonzero, its `cmpwi r3,0` takes the failure path and
  // the game goes on without the script callback that panicked, the way it would on a real error.
  {
    uint32_t sp = ctx.r1.u32;
    for (int i = 0; i < 64 && sp; ++i) {
      if (!legible(sp)) break;
      const uint32_t prev = leer32(sp);
      if (prev <= sp || !legible(prev - 8)) break;
      if (leer32(prev - 8) == 0x82516554) {
        ctx.r1.u32 = leer32(prev);  // the wrapper's own frame
        ctx.r3.u64 = 1;             // nonzero: the wrapper reports failure
        REXLOG_WARN("[exit]   pánico devuelto al llamador protegido (0x82516554, marco {:08X}): el juego "
                    "sigue sin el callback de Lua",
                    ctx.r1.u32);
        return;
      }
      sp = prev;
    }
  }

  REXLOG_ERROR("[exit]   sin código de recuperación tras la llamada: saliendo de verdad");
  __imp__ds1_exit_wrapper(ctx, base);
}

#endif
