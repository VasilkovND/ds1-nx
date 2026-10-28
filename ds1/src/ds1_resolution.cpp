// ds1-nx - the scene resolution chosen in the options menu (ds1_resolution.h), applied where the game sets up its
// display: 0x825C72A0 writes the display size the renderer reads (1280x720 in HD, 1024x768 in SD) and passes the same
// size to Direct3D's presentation parameters (the back buffer, created inside it, stays 1280x720). After it runs,
// 960x540 replaces 1280x720 when that is the choice. Whether every part of the renderer follows is checked on the
// console (PORTING.md).

#include "ds1_pch.h"

#include <rex/logging.h>
#include <rex/memory.h>

#include "ds1_resolution.h"

REX_EXTERN(__imp__ds1_ConfigureDisplay);
REX_EXTERN(ds1_ConfigureDisplay) {
  __imp__ds1_ConfigureDisplay(ctx, base);
  namespace r = ds1::resolucion;
  const uint16_t ancho = rex::memory::load_and_swap<uint16_t>(REX_RAW_ADDR(r::kAnchoEscena));
  const uint16_t alto = rex::memory::load_and_swap<uint16_t>(REX_RAW_ADDR(r::kAltoEscena));
  const uint32_t guardada = r::Guardada();
  const uint32_t elegida = guardada ? guardada : r::PorDefecto();
  if (elegida == 540 && ancho == 1280 && alto == 720) {
    rex::memory::store_and_swap<uint16_t>(REX_RAW_ADDR(r::kAnchoEscena), 960);
    rex::memory::store_and_swap<uint16_t>(REX_RAW_ADDR(r::kAltoEscena), 540);
  }
  REXLOG_INFO("ds1: escena {}x{} (el juego puso {}x{}; elegida {}p, {}; CPU {} MHz)",
              rex::memory::load_and_swap<uint16_t>(REX_RAW_ADDR(r::kAnchoEscena)),
              rex::memory::load_and_swap<uint16_t>(REX_RAW_ADDR(r::kAltoEscena)), ancho, alto, elegida,
              guardada ? "guardada" : "por defecto", r::CpuMhz());
}
