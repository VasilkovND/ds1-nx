// ds1-nx - the game's Lua errors (setjmp/longjmp) on the host.
//
// The game scripts run on Lua 5.0. A Lua error (luaD_throw, 0x825F5BF8, and its inlined copies in luaG_errormsg,
// luaD_growCI, luaD_call ...) is a longjmp to the jmp_buf the innermost luaD_rawrunprotected (0x825F5C40) armed with
// setjmp: that is how lua_pcall and lua_resume catch it and hand the script's error back to the game.
//
// In recompiled code each guest function is a host function, so the CRT's longjmp (0x828A3BB0), run as plain
// recompiled code, only loads the guest registers the setjmp saved (r1, r13-r31 ...) and then RETURNS to its caller:
// the host never leaves the function that raised the error. The script kept running after the error with the stack
// pointer and the non-volatile registers of another frame. Chapter 3's decontamination room is the first place a
// script raises an error (a call to a value that is no function), and what followed was luaD_precall reading a null
// closure (guest 0x6) and, with each guard put there, another crash a little further on (0xC, 0x9, 0x15, 0x0) or a
// script that never finished.
//
// Here luaD_rawrunprotected is native: the same guest frame and lua_longjmp (the guest Lua reads its status and its
// previous link), with a host setjmp instead of the guest's; and the guest longjmp to one of those buffers is a host
// longjmp back to it. The guest's other calls of setjmp (0x82A4825C, 0x82A483DC, 0x82B27320, 0x82B278E8) are not
// Lua's: a longjmp to their buffers is still the recompiled one, which does not unwind the host.

#include "ds1_pch.h"

#include <atomic>
#include <csetjmp>
#include <string>

#include <rex/logging.h>

namespace {

// lua_State (Lua 5.0, 32 bits; a TObject is { int tt; Value value }, 8 bytes).
constexpr uint32_t kLuaTop = 8;
constexpr uint32_t kLuaStack = 28;
constexpr uint32_t kLuaErrorJmp = 80;
constexpr uint32_t kLuaTString = 4;  // LUA_TSTRING

// luaD_rawrunprotected's frame (stwu r1,-1472(r1)) and its struct lua_longjmp at r1+80.
constexpr uint32_t kFrameSize = 1472;
constexpr uint32_t kFrameLongjmp = 80;
constexpr uint32_t kLongjmpPrevious = 0;
constexpr uint32_t kLongjmpBuffer = 16;
constexpr uint32_t kLongjmpStatus = 1360;
constexpr uint32_t kReturnFromProtected = 0x825F5CA0;  // the address after its bctrl, for guest call chains

// One luaD_rawrunprotected in progress on this thread.
struct ProtectedCall {
  jmp_buf host;
  ProtectedCall* outer;
  uint32_t guest_buffer;  // what the guest passes to longjmp to get back here
  uint8_t non_volatiles[PPCContext::kNonVolatileSaveSize];
};

thread_local ProtectedCall* t_protected = nullptr;

// The error object luaD_throw left on top of the Lua stack: the message, when it is a string.
void LogError(uint8_t* base, uint32_t L, uint32_t status) {
  static std::atomic<uint32_t> errors{0};
  const uint32_t n = errors.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n > 32 && (n & 0xFF) != 0) return;

  std::string text = "(no message)";
  const uint32_t top = REX_LOAD_U32(L + kLuaTop);
  const uint32_t stack = REX_LOAD_U32(L + kLuaStack);
  if (stack && top >= stack + 8) {
    const uint32_t tt = REX_LOAD_U32(top - 8);
    const uint32_t string = REX_LOAD_U32(top - 4);
    // TString: CommonHeader (next, tt at +4, marked), reserved, hash at +8, len at +12, then the characters.
    if (tt == kLuaTString && string && REX_LOAD_U8(string + 4) == kLuaTString) {
      const uint32_t length = REX_LOAD_U32(string + 12);
      if (length <= 4096) {
        text.assign(reinterpret_cast<const char*>(REX_RAW_ADDR(string + 16)), length < 400 ? length : 400);
        for (char& c : text) {
          if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) c = ' ';
        }
      }
    } else {
      text = "(error object of Lua type " + std::to_string(tt) + ")";
    }
  }
  REXLOG_WARN("ds1: Lua error {} (status {}, lua_State {:08X}): {}", n, status, L, text);
}

}  // namespace

// int luaD_rawrunprotected(lua_State* L, Pfunc f, void* ud)
REX_EXTERN(sub_825F5C40) {
  const uint32_t L = ctx.r3.u32;
  const uint32_t f = ctx.r4.u32;
  const uint32_t ud = ctx.r5.u32;
  const uint64_t entry_r1 = ctx.r1.u64;
  const uint64_t entry_r31 = ctx.r31.u64;
  const uint64_t entry_lr = ctx.lr;
  const uint32_t frame = ctx.r1.u32 - kFrameSize;
  const uint32_t lj = frame + kFrameLongjmp;

  // The original's prologue (the crash handler's call chain walks these), then
  // lj.status = 0; lj.previous = L->errorJmp; L->errorJmp = &lj;
  REX_STORE_U32(ctx.r1.u32 - 8, uint32_t(entry_lr));
  REX_STORE_U64(ctx.r1.u32 - 16, entry_r31);
  REX_STORE_U32(frame, ctx.r1.u32);
  REX_STORE_U32(lj + kLongjmpStatus, 0);
  REX_STORE_U32(lj + kLongjmpPrevious, REX_LOAD_U32(L + kLuaErrorJmp));
  REX_STORE_U32(L + kLuaErrorJmp, lj);

  ProtectedCall call;
  call.outer = t_protected;
  call.guest_buffer = lj + kLongjmpBuffer;
  ctx.SaveNonVolatiles(call.non_volatiles);
  t_protected = &call;

  if (setjmp(call.host) == 0) {
    // (*f)(L, ud);
    ctx.r1.u64 = frame;
    ctx.r3.u64 = L;
    ctx.r4.u64 = ud;
    ctx.lr = kReturnFromProtected;
    REX_CALL_INDIRECT_FUNC(f);
  } else {
    // A Lua error: the functions between here and the longjmp were abandoned half way, with whatever they had put in
    // the non-volatile registers. The guest setjmp would give back the ones it saved here.
    ctx.RestoreNonVolatiles(call.non_volatiles);
    LogError(base, L, REX_LOAD_U32(lj + kLongjmpStatus));
  }
  t_protected = call.outer;

  // L->errorJmp = lj.previous; return lj.status;
  REX_STORE_U32(L + kLuaErrorJmp, REX_LOAD_U32(lj + kLongjmpPrevious));
  ctx.r3.u64 = REX_LOAD_U32(lj + kLongjmpStatus);
  ctx.r1.u64 = entry_r1;
  ctx.r31.u64 = entry_r31;
  ctx.lr = entry_lr;
}

// void longjmp(jmp_buf env, int value)
REX_EXTERN(__imp__sub_828A3BB0);
REX_EXTERN(sub_828A3BB0) {
  const uint32_t guest_buffer = ctx.r3.u32;
  for (ProtectedCall* call = t_protected; call; call = call->outer) {
    if (call->guest_buffer == guest_buffer) {
      t_protected = call;  // the protected calls inside it are gone with their frames
      longjmp(call->host, 1);
    }
  }
  __imp__sub_828A3BB0(ctx, base);  // not Lua's
}
