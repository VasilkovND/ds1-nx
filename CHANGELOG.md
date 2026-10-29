# Changelog

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
