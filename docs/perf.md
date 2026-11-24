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
   G200eR2 (RetroGPU `gl/docs/rig.md`) measures the chip's time per triangle; the
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
| `FW2` | the mission: a whole job by the job autopilot, depot 6 to depot 0 bay 0 (`-job 6:0:0 -autojob`), one tick per frame, vsync off: reversing under the trailer in depot 6's yard, out through the gate, 1.65 km of road through the middle of town 4 (its roundabout, shops and church) with the traffic, into depot 0's yard, a loop round and the reverse into the bay; 12,731 frames (12,700 timed), the same at every preset |
| `FW3-couple`, `FW3-town`, `FW3-dock` | FW2's highlights (`-highlights`), for DOSBench: the same job, its three pieces timed as three tests and the drive between them fast-forwarded (eight steps a frame, a cleared screen, not timed): the start to 4 s after the coupling (875 frames timed), within 110 m of town 4's middle (1,539), the reverse from the trailer's axle 22 m from the bay to the delivery (1,442); 5,027 frames in all. `FW3L` and `FW3H` the same at LOW and HIGH |
| `FWP-<tris>-arr`, `-list`, `-imm` | 1,000 to 8,000 triangles of a ground grid under the game's camera through vertex arrays, display lists, immediate mode |
| `FWP-<tris>-arr-tex` | the same, textured (nearest, RGB565) |
| `FWP-2000-d<draws>` | 2,000 triangles split into 50 to 400 draw calls |

The mission's triangles a frame (headless, mean; FW2's most in brackets):

| Preset | FW2 | FW3-couple | FW3-town | FW3-dock |
|---|---|---|---|---|
| LOW | 1,083 (1,633) | 996 | 1,343 | 1,178 |
| MEDIUM | 2,759 (6,474) | 1,779 | 3,926 | 2,137 |
| HIGH | 3,774 (12,947) | 2,099 | 6,000 | 2,521 |

FW2 is the official run; at 30 frames a second it takes about 7 minutes,
so DOSBench runs FW3 (about 2 at LOW on the G200).

From the FWP records a small model, frame_ms = a + b x triangles + c x
draws + d x textured triangles, is fitted per machine and sets the presets'
limits (with 15% headroom); FW1 checks the game against it. Changes to
DOS-GL's hot paths (RetroGPU `gl/src/gl/vertex.c`, `emit.c`) are made only when
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

DOS-GL built with its stage timers (`make gl-lib PROF=1` in RetroGPU; `DGL_STATS=2`)
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
212 ns (RetroGPU's `docs/loop-c-results.md`); if none overlapped the CPU
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

## The new look (2026-11-19)

Smooth terrain (37% of the vertices), the rounded lorry at MEDIUM and HIGH
(`models`), the camera coming down with speed (`pitch_min`: sky and fog at
MEDIUM and HIGH), fields cut true, round trees, hedges, houses and the
roads' edge lines at MEDIUM and HIGH, contact shadows, the route's
chevrons and the target's markers. Headless, triangles a frame, mean /
most:

| Preset | FW1 (`-autopilot -frames 3000`) | A whole job (`-autojob`) |
|---|---|---|
| LOW (`models` 0, `pitch_min` 52) | 1,500 / 1,992 | 1,505 / 2,102 |
| MEDIUM (`models` 1, `pitch_min` 24) | 3,792 / 5,500 | 3,659 / 6,762 |
| HIGH (`models` 2, `pitch_min` 20) | 4,730 / 7,060 | 4,339 / 8,004 |

(With the depots' numbers, hedges and lamps, the verges' bushes at MEDIUM
and HIGH and the towns' lamps at HIGH, 2026-11-20: MEDIUM's FW1 mean 278
more than the 3,514 timed on the G400 below, about 67 fps there by its
triangles.)

With the trunks and roundabouts (2026-11-20), and the HUD's shapes counted
too (about 92 triangles a frame, drawn from a texture: they had been 610,
uncounted, for one commit):

| Preset | FW1 | A whole job |
|---|---|---|
| LOW | 1,382 / 2,033 | 1,611 / 2,131 |
| MEDIUM | 3,696 / 6,791 | 3,726 / 6,303 |
| HIGH | 4,723 / 7,789 | 4,401 / 7,273 |

Fuller towns and 64 m chunks (2026-11-21). A chunk is culled whole, and
with 128 m ones a view sent DOS-GL several times the ground it showed: the
first cut of the fuller towns came to 14,219 triangles a frame in a town at
MEDIUM. With 64 m chunks (4,096 of them, 3 to 5 more draws a frame) the
same frame is 6,317, and with MEDIUM's houses leaner (chimneys, garden trees
and back windows mostly HIGH's) 5,173. Headless, mean / most:

| Preset | FW1 | A whole job |
|---|---|---|
| LOW | 968 / 1,287 | 1,100 / 1,591 |
| MEDIUM | 3,370 / 5,344 | 2,786 / 6,573 |
| HIGH | 4,888 / 9,502 | 3,776 / 13,634 |

LOW is about 30% cheaper than before (the smaller chunks), the towns
costing it nothing; MEDIUM is cheaper on average and as dear at its worst
(a town); HIGH spends its room in the towns. The pack grew from 10.5 MB to
14.1 MB.

HIGH's textures (2026-11-25) draw the same triangles from WORLDHI.PAK (16.2
MB, loaded only for HIGH: about 30 MB of world on a HIGH machine, against
14.1 on the others), textured with one atlas, one draw a chunk as before.
On a card the cost is texture fill and setup, which DOSBench's FWP-tex
records measure per machine; HIGH is for the Radeons.

Traffic (2026-11-20) adds a car's mesh where one is in sight: 50
triangles at LOW, 190 at MEDIUM, 466 at HIGH, and a 10-triangle shadow; on
FW1 and the job none came into view (the means moved by under 10). The
simulation's cost: about 6.5 us a tick on the host for 12 cars (24 for
40), against 16 for the rest of it.

FW1 no longer compares with the rows above: its tour starts at another
town (town 0), runs the other way round and goes round the roundabouts;
MEDIUM's worst frame is a stretch west of town 0 that the old FW1 never
drove. At the same places the world costs what it did: three poses on the
road from town 0 to town 1, round 2's pack against this one, differ by
-230 to +110 triangles at MEDIUM and HIGH and -58 to 0 at LOW.

LOW costs what it did: the pack's LOW run keeps its plainer world
(its mean rose 4% only because a town's streets, which had been drawn
only while one chunk of them was in view, now always are). The P3 with a
TNT2 M64 ran the old look at 167 fps; MEDIUM wants measuring there and on
the G400 and G450 (FW1). Its peaks are past the plan's first guess
(4,000); if the bench says so, the levers are the hedges (about 400
triangles a frame), the far plane and pitch_min, all in BUDGET.CFG or the
pack's runs. HIGH is for the Radeon and can grow.
86Box draws the haul at MEDIUM, with fog, at a few frames a second; that is
86Box, not a measurement.

## On real cards (2026-11-20)

bench-g400 (the IBM PC 300PL: a Pentium II, a G400 on the 440BX's AGP at
1x, under GLOS 7a89adc), RetroGPU main b090479; every run's DGL-WARP line
failures=0, the old build and the new in one GLOS boot, each test twice
(the pairs agreed within 0.2%). FW1's command line (`-fixed -novsync
-autopilot -frames 3000`), frames a second, mean / 99th percentile ms:

| Build, preset | fps | p99 ms | triangles a frame (the game's; the WARP's) |
|---|---|---|---|
| main (the old look), MEDIUM | 118.7 | 10.2 | 1,470; 950 |
| main, LOW | 120.2 | 10.4 | 1,424 |
| polish, LOW | 117.0 | 10.5 | 1,500 |
| polish, MEDIUM with GL fog | 21.6 | 88.6 | 3,522; 8,056 |
| polish, MEDIUM without fog (now: the haze) | 72.7 | 23.8 | 3,522; 1,583 |
| polish, MEDIUM, camera held high (`pitch_min` 52) | 84.8 | 20.5 | 2,954; 1,552 |
| polish, HIGH (zoom far) with GL fog | 21.5 | 86.8 | 4,402 |
| polish, HIGH, camera held high | 77.0 | 21.8 | 3,437 |

LOW costs what it did (3% slower, its 2% more triangles). GL fog was the
cost of the new look, not its triangles: DOS-GL cuts a fogged triangle up
until one fog factor a triangle follows GL's per-pixel fog, five times the
triangles here. Without it MEDIUM runs at 72.7 fps on a G400, the low
camera's longer view costing 14% over the high one.

The P3 (bench B: a Pentium III with a RIVA TNT2 M64 under GLOS 7a89adc,
DOS-GL's TNT2 back end, RetroGPU main b090479), the same job (the pairs
agreed within 0.1%):

| Build, preset | fps | p99 ms |
|---|---|---|
| main (the old look), MEDIUM | 170.7 | 7.5 |
| main, LOW | 170.5 | 7.5 |
| polish, LOW | 160.9 | 7.5 |
| polish, MEDIUM with GL fog | 70.8 | 24.9 |
| polish, MEDIUM without fog (now: the haze) | 112.0 | 11.9 |
| polish, HIGH (zoom far) with GL fog | 60.2 | 27.9 |

The TNT2 fogs more cheaply (1.6 times the frame, against the G400's 3.3),
and without fog MEDIUM runs at 112 fps there. LOW is 6% slower than the
old look on the P3, 3% on the 300PL. The haze build (1a6e46f) itself is
yet to be timed on either; it adds three overlay quads to the runs
"without fog" above.

