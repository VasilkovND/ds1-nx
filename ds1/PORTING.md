# Porting notes: Dead Space (2008)

Dead Space (Xbox 360, Visceral Games / EA, 2008) on the Nintendo Switch. The structure, tools, SDK patches and renderer
glue come from ds3-nx (Dead Space 3, `ds3/PORTING.md` there) and ds2-nx; this file records what differs and every
problem met.

## Status

| Step | State |
| --- | --- |
| Repository from ds3-nx: SDK `f961303` + patches 0001-0032 (0014, disc swap, dropped), build scripts, renderer glue | done |
| Codegen from `default.xex` (host `rexglue` v0.10.0 + Hell's Gate's patch, as ds3-nx) | done: 184 units, 89 branch targets named (three rounds), 6 traps made gotos, 0 left |
| Names from Dead Space 2 (`tools/match_funcs.py`) | 1,482 exact matches |
| NRO links | done (`ds1.nro` ~51 MB) |
| 1.0.0 | after run 14: the first level playable, 540p stock / 720p overclocked |
| On the console | runs: menus and the first level; 540p at stock clocks (runs 7-13) and 720p overclocked (run 14, short) tested |
| Shader library | 340 shaders, 2,829 DKSH (run 7: 95 % of the draws have both shaders) |
| Rendering | right in the first level (run 10), after three clear fixes (Problems met, runs 7-9) |
| Frame rate (720p, CPU 1785 MHz) | menus ~30 fps; in the level 20-27 fps, GPU 32-50 ms per frame at 1,000-2,700 draws |

## What is known about the game

| | |
| --- | --- |
| Title ID | `45410857` |
| Media ID | `6A216DCC`, version 0.0.0.2, disc 1/1 |
| Executable | `default.xex` (13.9 MB), image base `0x82000000`, entry `0x82894210`; no title update used |
| Data | `bigfile0.viv`, `bigfile1.viv` (3.4 GB each) |

### Files (SHA-256)

| File | SHA-256 |
| --- | --- |
| `default.xex` | `2a0adbcaecaf5deaeb801ffbbaedcb813617b19f1043187f43fd5f82c15ec7e3` |

## Hooked functions (`codegen/names.toml`)

| Address | Name | How it was found |
| --- | --- | --- |
| `0x823EE5E0` | XGRegisterPixelShader | exact match with Dead Space 2 |
| `0x823EE618` | D3DDevice_CreatePixelShader | fuzzy match (0.79) and its place right after XGRegisterPixelShader, as in Dead Space 2 |
| `0x823EE9C0` | XGRegisterVertexShader | fuzzy match 1.0 |
| `0x823EEAB8` | D3DDevice_CreateVertexShader | fuzzy match (0.91), right after XGRegisterVertexShader as in Dead Space 2 |
| `0x82892C10` | SetThreadPriority | exact match |
| `0x825C72A0` | the display setup | writes 1280x720 (HD) or 1024x768 (SD) to `0x82DBA0B4`/`0x82DBA0B8` and the same size to the presentation parameters |

The XGRegister hooks need their object size checked for this XDK and are off (`DS1_XGREGISTER`). The audio and GPU
wait hooks of ds3-nx are not compiled yet (their functions are not named).

## Resolution

The 540p mode replaces 1280x720 with 960x540 at `0x82DBA0B4` after the display setup (`src/ds1_resolution.cpp`). The
back buffer, created in the same function, stays 1280x720. Whether every render target follows the smaller size is
not known until the first run.

## Problems met

| Run | Problem | Fix |
| --- | --- | --- |
| 1 | `Call to invalid or unregistered function 0x826B1368`, then nothing: a lone `blr` (a job's completion callback) the analysis never found, after another missed function at 0x826B1348 | `tools/find_code_gaps.py` names the code between known functions (932 gaps); it now skips alignment zeros at a gap's end |
| 1 | `--split` (cutting gaps after each `b`/`blr`) cut real functions: 246 new branch targets | not used |
| 2-4 | One frame, then the main thread at 100 % in 0x824ABA70: it hands a request on its stack to the job manager and spins (`lwsync; lwz r11,84(r1)`) until the job writes its status. `patch_stack_access.py` made stack accesses plain and the barrier generates no code, so the compiler read the status once | stack accesses stay volatile in functions with `sync`/`lwsync`/`eieio` |
| 5 | `[FATAL] Jump target 0x824B8484 unresolved at bctr 0x824B8338`: gap and branch-target names inside 11 switch functions cut them, their jump tables no longer resolved | the 25 names inside those functions dropped |
| 6 | Calls to 0x82862528 and 0x82862540 (missed) | named |
| 6 | Two thirds of the draws skipped (no VS): the library had 91 of ~460 shaders. The console had dumped them; an incomplete FTP listing made it look otherwise | library rebuilt from 340 |
| 7 | Walls and floors see-through, translucent characters: the clear of the material target (00008701: stencil on, no depth write) was taken as a depth clear and wiped the G-buffer's depth | stencil-only clears leave the depth |
| 8 | Black light buffer: that clear no longer cleared the stencil, and the light volumes' stencil test failed | `sdk-patches/0033`: a stencil-only clear |
| 9 | Walls, Isaac and the space backdrop through each other: a second such clear, in depth-only mode (mode 5), still wiped the depth | stencil-only in any mode |
| 11 | Without telemetry, 540p stock: 21-29 fps, GPU 32-45 ms at 1,500-2,900 draws (GPU-bound). Geometry store full (1,722 evictions / 10 s); stable ranges checked every 2nd frame made the opening doors jump back | store 128 MB; ranges checked every frame again |
| 12 | Profiled (telemetry): 80 passes for 4,010 draws, scene passes 30 ms of which draws 16 ms; passes cut every 256 draws by the partial submits | partial submits every 1,024 draws |
| 13 | Same scenes, intervals over 1,500 draws: GPU 36.8 ms a frame (run 11: 42.3), fps 24.8 (24.5); light scenes 29-30 fps | open: still GPU-bound in heavy rooms |
| 14 | 720p, CPU 1785 MHz (overclocked; the choice saved from the menu): a short session at 29.7-30 fps, GPU 8-32 ms at up to 1,400 draws, no errors | - |
| 10 | Picture right; 17-29 fps at 540p stock, GPU 30-57 ms per frame at 1,500-4,600 draws; RWAudioCore mixer 66 % of a core (a per-sample IIR filter, 0x82644C38), XMA decoder 31 % | measured with telemetry on (1 ms stack sampling): to measure again without it |

Diagnostics: `telemetria.flag` beside the NRO turns on the CPU stack profile from the start (`logs/rex/perfil_pilas.flag` does
not work: the folder is cleared at start), `ds1.toml` with `ds1_atajos_depuracion = true` the ZL + ZR shortcuts.
