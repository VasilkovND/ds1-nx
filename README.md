# ds1-nx

Dead Space (Xbox 360, 2008) on the Nintendo Switch, as a homebrew NRO: a static recompilation of your own `default.xex`
with ReXGlue (rexglue-sdk-nx, `sdk/`) and a deko3d renderer, built the way ds3-nx (Dead Space 3) and ds2-nx (Dead
Space 2) are.

**Work in progress**: see `ds1/PORTING.md` for what works. Nothing from the game is in this repository: you need your
own copy (`default.xex`, `bigfile0.viv`, `bigfile1.viv`).

Build: `ds1/tools/build_mac.sh <folder with default.xex>` (devkitPro, deko3d; see the script's header).
On the console: `sdmc:/switch/ds1-nx/ds1-nx.nro`, with `default.xex`, `bigfile0.viv` and `bigfile1.viv` beside it.
