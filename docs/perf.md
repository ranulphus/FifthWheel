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

The mean sits within the working Low preset (about 2,700 triangles); the
peaks, in towns, do not, so F7's presets need the draw distance or a lower
level of detail there. The flourishes are capped (16 coins, 64 confetti,
32 dust: `-fxtest`), so they add at most about 230 triangles.
