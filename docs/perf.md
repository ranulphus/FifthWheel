# Performance

Target: a Pentium II 266 with a Matrox G200 at 640x480, 30 frames a second
at the Low detail level; faster machines and the G400/G450 get more.

DOS-GL transforms and sets up every triangle on the CPU, so a triangle
costs about the same at any resolution: the budget is counted in
triangles (and draw calls), not pixels.

## Where the numbers come from

1. **A starting guess** from DOS-GL's PRD (§10.2: "low hundreds of
   thousands" of triangles a second). At an assumed 120k/s:

   | Preset | Triangles/frame | Notes |
   |---|---|---|
   | Low | about 2,700 | terrain 250, roads 350, structures 600, props 500, lorry 450, HUD 250, minimap 200, particles 64 |
   | Medium | 4,000 | more particles, a wider view |
   | High | 6,500 | optional GL fog and textured detail |

2. **Real silicon before the bench:** DOS-GL's rig build on devserver's
   G200eR2 (DOSGL `docs/rig.md`) measures the chip's time per triangle; the
   CPU's share on a Pentium II comes from a model (86Box's emulated CPU
   time with the waits taken out, and instruction counts).
3. **The bench**, which decides: DOSBench runs Fifth Wheel's tests on each
   bench PC and keeps the records.

86Box's speed says nothing about real hardware (it runs on a busy host,
and its Matrox engine runs in host time): in Loop A these tests only prove
the machinery works.

## The tests (DOSBench `tools/games.json`, group `game`)

| Test | What it measures |
|---|---|
| `FW1` | 3,000 frames of the autopilot touring the generated world at one tick per frame with vsync off: the game as it is, frame times and triangles per frame |
| `FWP-<tris>-arr`, `-list`, `-imm` | 1,000 to 8,000 triangles of a ground grid under the game's camera through vertex arrays, display lists, immediate mode |
| `FWP-<tris>-arr-tex` | the same, textured (nearest, RGB565) |
| `FWP-2000-d<draws>` | 2,000 triangles split into 50 to 400 draw calls |

From the FWP records a small model, frame_ms = a + b x triangles + c x
draws + d x textured triangles, is fitted per machine and sets the presets'
limits (with 15% headroom); FW1 checks the game against it. Changes to
DOS-GL's hot paths (DOSGL `src/gl/vertex.c`, `emit.c`) are made only when
the model points at them.

## What the game draws (counted, 2026-10-01)

The kit counts the triangles and draw calls each frame submits (each mesh,
world chunk, text string and immediate-mode block is a draw) and logs the
mean and the most at the end (`FW-STAT`). Headless, identical on every
target:

| Run | Triangles a frame (mean, most) | Draws a frame (mean, most) |
|---|---|---|
| FW1 (`-autopilot -frames 3000`) | 2,144, 3,336 | 14, 18 |
| A whole job (`-autojob`): HUD, minimap, reversing camera, flourishes | 2,123, 3,720 | 36, 58 |

The mean sat within the working Low preset (about 2,700 triangles); the
peaks, in towns, did not. Most of that was out of view: `world_draw` drew
every 128 m chunk within 90 m of the camera's target. It now tests each
chunk's box (with its height range, 8 m of margin for triangles that reach
over) against the view's six planes and skips those wholly outside one;
the pictures are byte for byte the same:

| Run | Triangles a frame (mean, most) | Draws a frame (mean, most) |
|---|---|---|
| FW1, chunks culled | 1,466, 1,972 | 12, 13 |
| A whole job, chunks culled | 1,422, 2,132 | 33, 55 |

The flourishes are capped (16 coins, 64 confetti, 32 dust: `-fxtest`), so
they add at most about 230 triangles.

## Detail presets (`data/budget.cfg`, `game/src/detail.c`)

LOW, MEDIUM and HIGH set the far plane and the world's reach, the furthest
camera zoom, the flourishes' caps, the minimap's road detail and how far
away standing trailers are drawn; `BUDGET.CFG` beside the game overrides
the built-in numbers, so a bench session can retune them without a build.
The governor (not in tests) steps a preset down after 2 s averaging slower
than 36 ms a frame, and back up after 6 s faster than 25 ms.

FW1 (`-autopilot -frames 3000`), triangles a frame, mean / most:

| Preset | Zoom near | Zoom normal | Zoom far |
|---|---|---|---|
| LOW (zoom stops at normal) | 1,382 / 1,972 | 1,423 / 1,972 | (normal) |
| MEDIUM | 1,427 / 1,972 | 1,468 / 1,972 | 1,527 / 2,194 |
| HIGH | 1,430 / 1,972 | 1,480 / 1,972 | 1,554 / 2,194 |

The presets differ little: with the chunks culled, what the camera sees
is the cost, and the world's own triangles dominate it. Everything is
inside the working Low budget (about 2,700) already. If the bench says LOW
must cost less, the lever is in the pack: each chunk's decoration (trees,
hedges, props) last in its triangle list, so LOW can draw it only near the
rig.

## The CPU's share (Part C2, 2026-10-02)

DOS-GL built with its stage timers (`make PROF=1` in DOSGL; `DGL_STATS=2`)
and the tour (`-fixed -novsync -nosound -autopilot -frames 1500`) on the
emulated G200 (a Pentium II), summed with the HAL's `tools/perf/profsum.py`.
The cycles are the emulated CPU's, so this is a model, not a measurement:

| Stage | Cycles a triangle (LOW; MEDIUM the same within 2%) |
|---|---|
| the game (`app`: simulation, HUD, everything outside GL) | 138 |
| transform (`xform`) | 1,737 |
| projection, clipping, validation | 1,487 |
| set-up (DOS-GL's, and the HAL's plane, increments, trapezoid) | 1,886 |
| the swap, less its waits | 178 |
| **DOS-GL without waits** | **5,289** |

13 register writes a triangle. On devserver's G200eR2 a register write took
212 ns (MGA-Glide `docs/loop-c-results.md`); if none overlapped the CPU
that would add up to about 730 cycles at 266 MHz.

**Estimate for a Pentium II 266 with a G200:** 5,400 to 6,150 cycles a
triangle, about 43,000 to 49,000 triangles a second, CPU-bound (the chip
needs 3-4 us for a small triangle on the G200eR2; the CPU about 20). FW1 at
LOW (1,207 triangles a frame on average, 1,532 at most, in the first 1,500
frames) would then run at about 36-41 fps on average and 28-32 fps in its
heaviest frames: the 30 fps line is met on average and is close at the
peaks. Not yet cross-checked (C2's callgrind count) and 86Box's Pentium II
timing is approximate, so the bench still decides.

**Where the cycles go, and the levers:**
- Transform is a third. Every world quad has its own four vertices (each
  face lit flat: the faceted look), so each triangle transforms two; the
  64-slot cache (`vertex.c`) finds nothing to share between quads. Terrain
  with shared vertices (about 0.6 transforms a triangle) would save about
  1,100 cycles on its triangles but shade the ground smoothly. Decided
  (2026-10-03): the faceted look stays; the visuals are plain enough
  already for games of the era, so speed comes from the other levers.
- DOS-GL's array path (fetching `GL_SHORT` positions and `GL_UNSIGNED_BYTE`
  colours) is the triangle-path work's next target; it gains every game.
- LOW's numbers in `BUDGET.CFG` (the far plane above all) are the lever
  that needs no build.
