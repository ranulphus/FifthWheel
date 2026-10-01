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

## Sound (game/src/sound.c, kit/src/mix.c)

Every sound is synthesised at start-up: the diesel as two loops recorded at
1000 rpm (a deep one with a knock on each firing, a raspy one with a turbo
whistle) pitched by rpm and crossfaded from low to high between 900 and
1900 rpm; the air brakes' hiss, the reversing beeper, a puff on each gear
change; a two-tone horn (H, held) whose tones loop seamlessly (periods of
67 and 53 samples); a clunk when the fifth wheel locks, a thud on a bump,
a squeal on a jackknife, a chime for a delivery. The mixer ramps every
volume change over 3 ms, so starts, stops and crossfades do not click.

The audio device opens once the game has loaded and closes before it
cleans up: neither yields, and the Sound Blaster's ring holds about 90 ms.
On DOS the kit routes SDL's log through its own, and the driver's count
of chunks played as silence becomes a test check (`HX-TEST audio`).

## Career and garage (game/src/career.c, garage.c)

A career (`CAREER.DAT`) keeps the money, the deliveries and the garage's
items between runs; it is saved after each delivery and purchase and at
the end. The garage (G, stopped) is a showroom: the tractor turns on a
platform under the sky while the shop browses paint jobs, horns, decals,
licences and cabs, each with a price and a rarity whose colour lights the
card and the platform's rim (common grey, uncommon green, rare blue, epic
purple, legendary gold). What is shown is tried on before it is bought.
Box trailers come with the job; flatbeds and tankers need a licence, and
their jobs show greyed on the board until then. Tests, replays and
recordings leave the career alone (a test can name one with `-career`).

## Controls (game/src/input.c)

The keyboard's digital keys are smoothed so they steer like a wheel. A
joystick or wheel (a DOS gameport through SDL; up to four axes) gives the
steering, the accelerator and the brake as the set-up screen (J, or
`-calibrate`) mapped them: it asks for the controls at rest, full left,
full right, full accelerator and full brake, and takes whichever axis
moves in each step, so a stick (forward and back) and a wheel with
separate pedals both work whatever their axes' order or direction. The
map goes into the settings file (`FWHEEL.CFG` beside the game; written
to a temporary file first, then swapped in). Button 1 is the handbrake,
button 2 the horn. The keys steer while held, the stick otherwise; each
pedal takes the further of the two. SDL's gameport driver learns each
axis's range as it moves, in every run: until the stick has been to an
end once, part of the way reads as all of it.

## Jobs (game/src/jobs.c)

A trailer waits at every depot, parked along the apron's left edge, and
the job board there (keys 1-3) offers three jobs for it, each to a bay at
another depot. Pay grows with the distance by road (tankers pay a little
more). Backspace cancels a job until the trailer is coupled. Taking and
cancelling are input like the pedals, so replays carry them.

- **Coupling:** back the tractor under the trailer: the fifth wheel within
  0.6 m of the kingpin, lined up within 12 degrees, slower than 1.5 m/s.
- **The route:** the shortest way over the road graph (Dijkstra). The HUD's
  arrow points along it and the minimap draws it; more than 60 m off it,
  it is worked out again from the nearest junction.
- **Docking:** on the destination's apron the gauge shows the gap to the
  dock, the sideways error and the angle; stopped for 1.5 s within a
  grade's limits, the job is done. PERFECT: 0.25 m across, 0.5 m gap, 2
  degrees; GREAT: 0.5, 1.0, 4; GOOD: 0.9, 1.6, 7; OK: 1.5, 2.5, 12.
- **Pay:** the base times a time factor (1.2 on time, falling to 0.5 when
  late), less 4 per point of damage, plus a bonus for the grade (GOOD 40,
  GREAT 80, PERFECT 150); at least 10.
- Trailers change out of sight (250 m): a new one waits where one was
  taken, and a delivered one is unloaded and gone. Parked trailers are
  solid, from 2 m behind the kingpin (room for a tractor backing under).

Coupling and docking in reverse, the camera turns to look the way the rig
is backing (along the waiting trailer, or into the bay) and takes on a
car's reversing camera: guide lines on the ground where the rig's back
corners will go if it keeps reversing with the wheels where they are
(`rig_predict_reverse`, the physics' own kinematics, so they bend as you
steer), red to 1.5 m, yellow to 4 m, green to 10 m, with bars across; the
picture's edges darkened, viewfinder brackets and a blinking REAR CAM
label (`guides.c`, `hud.c`). `-dockpose` holds such a scene still for
pictures (`make shots`).

The job autopilot (`-autojob`, `jobpilot.c`) makes the inputs a player
would: it backs under the trailer, drives out through the gate, follows
the route, crosses the far apron to the right of the bay, loops round to
face the gate on the bay's line, and reverses in. Reversing, pure pursuit
on the trailer's axle gives the trailer's wanted curvature; the kingpin
kinematics turn that into a wanted articulation; the tractor steers for
the yaw rate that holds it. `make jobsweep` runs it on every depot pair
and bay.

## The generated roads (tools/fwgen.c)

Towns are joined by a minimum spanning tree and each town's two nearest
neighbours. Every depot is a dead end off its nearest town: its gate faces
that town and its road leaves straight for 50 m before it bends. No road
enters a depot's yard: the generator tries wider bends, and fails rather
than write a world where one does.
