// ds1-nx - Dead Space (Xbox 360, 2008) on the Nintendo Switch.
//
// Where the game is: beside the NRO (sdmc:/switch/ds1-nx/ on the console), the disc's default.xex, bigfile0.viv and
// bigfile1.viv (game:, and game_data_root). ds1.toml's ds1_game picks another folder.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system.h>
#include <rex/ui/overlay/achievement_notification.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "ds1_cpu.h"
#include "ds1_gpu.h"
#include "ds1_menu.h"
#include "ds1_resolution.h"

#if defined(__SWITCH__)
// The SDK's debug shortcuts (ZL + ZR held: Down capture, Left frame dump and draw trace, Right draw-skipping modes,
// R CPU stack sampler, L GPU lap; Up the FPS counter): off since 1.0.0 except the FPS counter; ds1.toml turns them on
// for bug reports.
REXCVAR_DECLARE(bool, switch_atajos_depuracion);
REXCVAR_DEFINE_BOOL(ds1_atajos_depuracion, false, "DS1",
                    "Atajos de depuracion con ZL+ZR (captura, volcado de fotograma, modos de omision, muestreo de "
                    "pilas con R). false = solo el contador de FPS");
#endif

#if defined(__SWITCH__)
#include <rex/ui/switch_apm.h>
#endif
// Handheld GPU clock through Horizon's own performance profiles (the ones commercial games pick; no overclock): at
// stock the handheld GPU runs at 307.2 MHz, and run 22 needed 31-37 ms of GPU per frame at 540p. 460 asks for
// 0x92220008 (GPU 460.8 MHz, CPU 1020 and memory 1331.2 as before; the SDK's switch_apm.cpp, from the NFS port, tries
// the pairs that keep the memory clock first and puts it back if it rises). Docked, or with sys-clk setting the
// clocks, nothing changes.
REXCVAR_DEFINE_INT32(ds1_gpu_portatil_mhz, 460, "DS1",
                     "GPU en modo portatil por los perfiles de rendimiento de Horizon: 460 o 384 MHz; 0 = no tocar "
                     "(307,2 MHz de serie)")
    .range(0, 460);

REXCVAR_DEFINE_STRING(ds1_game, "", "DS1", "Carpeta del juego (default.xex, bigfile*.viv). Vacio: junto al ejecutable.");

class Ds1App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Ds1App>(new Ds1App(ctx, "ds1", PPCImageConfig));
  }

 protected:
  // No Xenos emulation: ds1_gpu.cpp reads the game's command ring and draws with the deko3d renderer.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics) config.graphics = ds1::gpu::CreateGraphicsSystem();
  }

  // game_data_root is the folder with default.xex: ds1_game, or the NRO's own folder.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    if (const std::string flag = REXCVAR_GET(ds1_game); !flag.empty()) {
      paths.game_data_root = flag;
      return;
    }
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (!folder.empty()) paths.game_data_root = folder;
  }

  void OnPostInitLogging() override {
    // The SDK sends the controllers zero vibration unless input_vibracion is on; on, the game's own vibration setting
    // decides (ds2-nx 1.2.2). ds1.toml can still turn it off.
    DefaultFlag("input_vibracion", "true");
    // The game's vibration is for the Xbox 360 pad's motors; the Switch's HD Rumble at the same amplitude is far
    // stronger, so half by default (sdk-patches/0032; the options menu changes it and keeps the choice in
    // ds1_vibracion.txt).
    DefaultFlag("input_vibracion_fuerza", "0.5");
    // The ring thread's per-draw cost (run 10: ~9 us a draw at 1,500-4,600 draws a frame, half of it the vertex ranges'
    // fingerprints). The geometry store was full (60 of 64 MB, 1,722 evictions in 10 s, each re-uploaded and hashed
    // again): 128 MB. Stable ranges are still checked every frame (nfsmw_dk_geometria_confianza 1): checked every 2nd
    // frame, the opening doors (animated on the CPU) jumped back and repeated (run 11).
    DefaultFlag("nfsmw_dk_geometria_mb", "128");
#if defined(__SWITCH__)
    REXCVAR_SET(switch_atajos_depuracion, REXCVAR_GET(ds1_atajos_depuracion));
#endif
    ds1::cpu::InformarSistema();
  }

  // After Runtime::Setup (the VFS exists), before LoadXexImage.
  void OnLoadXexImage(std::string& xex_image) override {
    std::error_code ec;
    for (const char* name : {"default.xex", "bigfile0.viv", "bigfile1.viv"}) {
      if (!std::filesystem::is_regular_file(game_data_root() / name, ec)) {
        Fail(fmt::format("Dead Space: {} is missing from {} (INSTALL.md).", name, game_data_root().string()));
      }
    }
    REXLOG_INFO("ds1: game at {}; loading {}", game_data_root().string(), xex_image);
  }

  void OnPreLaunchModule() override {
    ds1::cpu::Configurar();
    ds1::menu::Instalar(*runtime());
#if defined(__SWITCH__)
    // Not on an overclocked console: there the profile became the clocks that stayed (run 27: sys-clk had the CPU at
    // 1785 and the GPU at 614.4 at start, and from the first seconds on everything ran at the profile's 1020 / 460.8 /
    // 1331.2, the fights CPU-bound at 23 fps; in runs 25 and 26 sys-clk had put its clocks back).
    const uint32_t cpu_mhz = ds1::resolucion::CpuMhz();
    if (const int32_t mhz = REXCVAR_GET(ds1_gpu_portatil_mhz); mhz > 0 && cpu_mhz <= 1020) {
      RexSwitchApmPedirGpuMhz(mhz);
      RexSwitchApmAplicar();
    } else if (mhz > 0) {
      REXLOG_INFO("ds1: CPU at {} MHz (overclocked): the handheld GPU profile is not requested", cpu_mhz);
    }
#endif
  }

  // No achievement toast. Its ImGui dialog, created with the first achievement (the end of the prologue), is a UI
  // drawer for the rest of the session, and with one the presenter paints from the UI thread on request: from that
  // moment (run 17) the screen stayed black with the sound going, then flickered. The achievement is still unlocked
  // and saved.
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override {
    REXLOG_INFO("ds1: achievement toast off (it would move presentation to the UI thread)");
    return nullptr;
  }

 private:
  // A flag's value unless ds1.toml (or the command line) already set it.
  static void DefaultFlag(const char* name, const char* value) {
    if (!rex::cvar::GetFlagInfo(name) || rex::cvar::GetFlagSource(name) != rex::cvar::Source::kDefault) return;
    rex::cvar::SetFlagByName(name, value);
  }

  [[noreturn]] static void Fail(const std::string& msg) {
    REXLOG_ERROR("{}", msg);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    rex::FlushLogging();
    std::_Exit(1);
  }
};
