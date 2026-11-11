# Changelog

## 1.0.6

The chapter 3 crash, third round: 1.0.5's `[parche]` lines proved the file bytes were patched, yet the script still
ran with `SetWatchValue` - it travels inside a compressed bundle section that the game decompresses in guest memory
after the read, so the patch never saw it (and the panic-recovery net left the Lua state dirty and crashed three
milliseconds later). The fix now lives where every script string must pass after decompression: the Lua string
interner (0x825FBC38, hooked as `ds1_lua_intern`) rewrites the never-registered watch API names (`SetWatchValue`,
`ClearWatchValue`, `GetWatchValue` → `GetSwarmCount`, `SetWatch`/`GetWatch`/`ClearWatch` → `type`, never longer than
the name it overwrites), logged as `[intern]` lines. Only `ds1-nx.nro` changes.

## 1.0.5

The chapter 3 crash, round two. 1.0.4's two fixed offsets were not the copy of the script the game runs (the
error stayed `SetWatchValue`), so the byte patch is now universal: every bigfile0/1.viv read is scanned and every
"SetWatchValue" replaced with "GetSwarmCount", whatever copy or bundle section it travels in; replacements are
logged as `[parche]` lines. On top of that, a safety net: a Lua panic that still reaches the game exit is unwound
back to the protected-call wrapper (sub_82516488, return 0x82516554) with a script-failure code, so the game keeps
running without the failed callback instead of exiting. Only `ds1-nx.nro` changes.

## 1.0.4

The chapter 3 crash, fixed at its cause. 1.0.3's Lua-state dump caught the error message: 90 seconds into
chapter 3 the flicker light effect runs the shared audio script `assets\shared\audio\luascripts\_common`,
whose compiled Lua calls the global `SetWatchValue` — a tuner-only binding this build of the game never
registers — so the call hits nil, Lua panics (no protected frame), the game calls exit(1) and the teardown
crashes. New SDK patch 0034: HostPathFile::ReadSync substitutes, on the fly, the 13 bytes of "SetWatchValue"
in the script's constant pool (the two copies in bigfile0.viv, at 946394074 and 2343295208) with
"GetSwarmCount", a registered query of the same length that ignores its arguments. The game data on the SD
is not touched; a mismatching bigfile only logs. bigfile1.viv has no copies (scanned in full).

## 1.0.3

Diagnosis build for the chapter 3 Lua panic. 1.0.2's [exit] dump named the failing script — the flicker light effect
(`flicker_start` / `flicker_update` / `flicker_end` on the stack, panic at luaD_throw 0x825ED874, ~90 s into chapter 3)
— but not the Lua error itself. The exit hook now also dumps the Lua state (r31 at the panic) and every string its
stack slots point at, TString header offsets included, so the error message ("attempt to …") lands in the log as
`[exit] pilaLua` lines. The panic site has no recovery code after its exit call, so the game still exits; this build
exists to capture the message the real fix needs. Only `ds1-nx.nro` changes.

## 1.0.2

The chapter 3 crash, second round: 1.0.1's unregistered functions were part of the game's exit path, not the cause. A
Lua error during the chapter 3 load escalates to a Lua panic that calls exit(1), and the process teardown after it is
what crashed (the "read of guest 0x6"). The exit wrapper (0x828A09F8) is hooked now: it logs everything the panic left
(registers, guest-stack return addresses, strings — the Lua error message among them), and where the game has its own
recovery code after the exit call (4 of the 8 panic sites), the exit is skipped and that code runs instead, so the game
keeps going. Only `ds1-nx.nro` changes.

## 1.0.1

Fixed the crash at the chapter 3 load (the game exited to the home menu every time): the exit-time callback table,
filled at run time by registration thunks, names 343 small functions the codegen had not generated; three were called
at the chapter 3 load and logged as "unregistered function". All are named now (`atexit_cb_*` in codegen/names.toml).
Only `ds1-nx.nro` changed.

## 1.0.0

First release: Dead Space (Xbox 360, 2008) on the Switch, playable through the first level as tested. 540p / 30 fps at
stock clocks in most places (heavy fights and rooms dip to about 25 fps), 720p / 30 fps overclocked.

- Built the way ds3-nx is: the codegen from your `default.xex` (no title update), ds3-nx's renderer glue, options menu
  (720p / 540p, vibration strength) and SDK patches 0001-0032 (0014, the disc swap, not needed).
- Code the analysis does not reach (`tools/find_code_gaps.py`): 932 gaps named; gaps inside switch functions left out.
- The stack pass (`patch_stack_access.py`) keeps the stack volatile in functions with a memory barrier: the main
  thread spun forever waiting for a job's status on its stack.
- Direct3D clears with the stencil on and depth writes off clear only the stencil (`sdk-patches/0033`): walls,
  characters and the sky showed through each other, and the light buffer was black.
- Hooks, each compiled when its function is named: Direct3D's GPU wait, SetThreadPriority, the gamma ramp (in-game
  brightness), RenderWare Audio's mixer loops on NEON, its compressor bypass, the mixer thread's priority and the
  null-pool guard in StreamPool::AcquireStream.
- Renderer defaults: geometry store 128 MB, partial submits every 1,024 draws.
