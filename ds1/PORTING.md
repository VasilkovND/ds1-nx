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
| NRO links | done (`ds1.nro` ~50 MB) |
| On the console | not run yet |

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
