# Design

The plan (milestones F0-F8, the kit, the performance budget, the tests)
lives with the project notes; this file records what is built.

## Layout

| Path | What |
|---|---|
| `kit/include/dgk/`, `kit/src/` | dgk: `base` (arenas, CRC, PCG32), `log`, `app` (the loop), `plat_sdl.c` / `plat_headless.c`, `gfx`, `text`, `mix`, `test` |
| `kit/tools/fontbake.py` | bakes a glyph file into a font atlas (outline and drop shadow) |
| `game/src/` | Fifth Wheel itself |
| `data/` | the sources of every asset |

## The loop

Fixed 60 Hz ticks, at most four per frame (after a stall the game slows
down rather than spiralling), then a draw with the fraction of a tick
elapsed, then the swap. SDL gets a turn at the start of each frame, after
each tick and before the swap: on DOS its threads (the Sound Blaster
driver's among them) switch only at those yields, and its ring holds about
45 ms. `-fixed` runs exactly one tick per frame, so frame N is the same
picture on every target.

## Three targets, one GL subset

Every target compiles its OpenGL calls against DOS-GL's own `<GL/gl.h>`:
anything DOS-GL lacks fails to compile on Linux too. The DOS build links
DOSGL's SDL3 (with its DOS-GL bridge) and `libGL.a`; the Linux build links
the same pinned SDL3 built for Linux and the system's OpenGL; the headless
build replaces SDL with OSMesa and a virtual clock. The Linux and headless
builds run in the harness's dev container (`build/deps/` stages what they
need from DOSGL).
