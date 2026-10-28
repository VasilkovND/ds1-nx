// ds1-nx - the scene resolution: the game's own 1280x720 or 960x540 (three quarters, the same shape). 960x540 is for
// stock clocks: the GPU does about 56 % of the pixel work. The game sets its scene size once, when it sets up the
// display (ds1_resolution.cpp, the hook on 0x825C72A0), so a change takes effect at the next start. (From ds3-nx,
// where the scene is 1152x720.)
//
// The choice is in ds1_resolucion.txt beside the NRO ("720" or "540"), written by the options menu (ds1_menu.cpp,
// ZL + ZR + Minus). Without it: 720 when the CPU runs at 1.5 GHz or more (an overclocked console), else 540.
// Header-only: the hook is built into the game module and the menu into the title, which share no variables.

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>

#include <rex/filesystem.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace ds1::resolucion {

// Where the game keeps its display size (big-endian u16), written by 0x825C72A0 (1280x720 in HD, 1024x768 in SD).
constexpr uint32_t kAnchoEscena = 0x82DBA0B4;
constexpr uint32_t kAltoEscena = 0x82DBA0B8;

inline std::filesystem::path Fichero() { return rex::filesystem::GetExecutableFolder() / "ds1_resolucion.txt"; }

// The saved choice (720 or 540), 0 if there is none.
inline uint32_t Guardada() {
  FILE* f = std::fopen(Fichero().string().c_str(), "r");
  if (!f) return 0;
  unsigned valor = 0;
  const bool leido = std::fscanf(f, "%u", &valor) == 1;
  std::fclose(f);
  return leido && (valor == 720 || valor == 540) ? valor : 0;
}

inline bool Guardar(uint32_t valor) {
  FILE* f = std::fopen(Fichero().string().c_str(), "w");
  if (!f) return false;
  const bool escrito = std::fprintf(f, "%u\n", unsigned(valor)) > 0;
  return std::fclose(f) == 0 && escrito;
}

// The CPU clock in MHz (0 if it cannot be read).
inline uint32_t CpuMhz() {
#if defined(__SWITCH__)
  uint32_t hz = 0;
  if (R_SUCCEEDED(clkrstInitialize())) {
    ClkrstSession sesion;
    if (R_SUCCEEDED(clkrstOpenSession(&sesion, PcvModuleId_CpuBus, 3))) {
      clkrstGetClockRate(&sesion, &hz);
      clkrstCloseSession(&sesion);
    }
    clkrstExit();
  }
  return hz / 1000000;
#else
  return 0;
#endif
}

inline uint32_t PorDefecto() {
  const uint32_t mhz = CpuMhz();
  return mhz == 0 || mhz >= 1500 ? 720 : 540;
}

inline uint32_t Elegida() {
  const uint32_t guardada = Guardada();
  return guardada ? guardada : PorDefecto();
}

}  // namespace ds1::resolucion
