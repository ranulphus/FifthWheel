# Design

The plan (milestones F0-F8, the kit, the performance budget, the tests)
lives with the project notes; this file records what is built.

## Layout

| Path | What |
|---|---|
| `~/dgk` (its main) | dgk, once `kit/` here: `base` (arenas, CRC, PCG32), `log`, `app` (the loop), `plat_sdl.c` / `plat_headless.c`, `gfx`, `text`, `mix`, `test`; `tools/fontbake.py` bakes a glyph file into a font atlas (outline and drop shadow) |
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
DOS-GL's SDL3 (with its DOS-GL bridge) and `libGL.a`; the Linux build links
the same pinned SDL3 built for Linux and the system's OpenGL; the headless
build replaces SDL with OSMesa and a virtual clock. The Linux and headless
builds run in the harness's dev container (`build/deps/` stages what they
need from RetroGPU's `gl/`).

## Sound (game/src/sound.c, dgk's src/mix.c)

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

## Screens (game/src/menu.c)

Playing starts at the title screen: the tractor turning in the showroom
under the name, with DRIVE, GARAGE, OPTIONS, JOYSTICK SET-UP and QUIT.
Esc while driving pauses (RESUME, OPTIONS, JOYSTICK SET-UP, QUIT); the
world waits under every screen. The options set the detail preset, the
screen size (from the next start: the game hands it to the kit as
`-mode`), vsync, the volume and the governor, and keep them in
`FWHEEL.CFG`. Tests, replays and recordings start driving at once (a test
can ask for `-title`); while recording or replaying, Esc still quits.

## Detail (game/src/detail.c)

Each preset is meant for a card (the owner, 2026-11-19): LOW for the
Matrox G200 (the minimum machine, a Pentium II 266, at 30 frames a
second), MEDIUM for the G400, G450 and the RIVA TNT2, HIGH for the ATI
Radeon 7200 and 7500 once DOS-GL drives them (a Radeon transforms vertices
itself, so HIGH need not keep to what a CPU can transform). The game
starts at the preset meant for the card it finds (`detail_for_renderer`,
from DOS-GL's renderer name; a desktop's own OpenGL gets HIGH) unless
`-detail` or the options' choice in `FWHEEL.CFG` says otherwise; tests start
at MEDIUM on every card, so a frame is the same picture on each.

LOW, MEDIUM and HIGH (`docs/perf.md`) set the far plane, the furthest zoom
(Z cycles near, normal and far), the flourishes' caps, the minimap's road
detail, the standing trailers' distance, how round the models are and how
low the camera comes; `BUDGET.CFG` overrides the numbers. The governor
watches the frame time and steps a preset down when frames run slow, back
up when there is room again; it is off in tests and with `-fixed`.

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

## Flourishes (game/src/fx.c)

Looks only, never fed back into the simulation: a delivery's pay flies to
the wallet as coins (each lands with a ting) while the money counts up; a
GREAT or PERFECT dock pops confetti from the screen's bottom corners, each
piece flipping as it falls; dust puffs where the wheels brake hard or spin
and where the rig couples, jackknifes or hits something; the cab rocks on
its springs, leaning out of turns, squatting as it pulls away, diving as
it brakes and jolting on knocks. Callouts squash and stretch as they land.
Each kind lives in a fixed pool (16 coins, 64 confetti, 32 dust: a new one
takes an old one's place), so a frame's cost has a ceiling (`-fxtest`).

## The look (game/src/mesh.c, lorry.c, tools/fwgen.c)

Cartoon and smooth, in the manner of the Nintendo 64 and GameCube rather
than the faceted low-poly of before (the owner's direction, 2026-11-19),
one art style scaled by the detail presets so that LOW still holds 30
frames a second on a Pentium II 266 with a G200.

- **The terrain** is smooth-shaded: each height sample a vertex lit by its
  own normal, shared by the cells round it that lie in the same field. At
  MEDIUM and HIGH the fields' edges run true: a cell whose corners lie in
  fields of different colours is cut along the boundary (straight between
  two fields' seeds, chords round a place's grass), its pieces on its own
  triangles, so the ground is where it was; at LOW such a cell is whole,
  in its middle's colour, in 16 m steps as before. The pack keeps both
  (`CVAR`, docs/formats.md): each chunk's triangles are LOW's run, the
  shared run, MEDIUM and HIGH's run and HIGH's own, one draw call at any
  detail. The
  shared terrain has 37% of the faceted terrain's vertices (DOS-GL
  transforms each once), and fwgen keeps each run's identical vertices once
  (the corners the road's quads and the cut terrain's pieces share: a
  quarter fewer). The pack is 10.0 MB (the plan's limit is 12),
  loaded whole: LOW carries about 5 MB of MEDIUM and HIGH's run it never
  draws, and the 32 MB machine's heap (the mem suite: within 20 MB) is
  mostly the pack. If it grows further, MEDIUM and HIGH's run goes into a
  pack of its own that LOW does not load.
- **The lorry** is as round as the preset's `models` says (`BUDGET.CFG`): 0
  (LOW) the boxes it always was; 1 (MEDIUM) and 2 (HIGH) a rounded cab
  with a wrap-round windscreen, side windows, mirrors, a grille and round
  lamps, round wheels with hubs, fuel tanks and mudguards, a rounded
  trailer with a white roof, doors and tail lamps, a domed tanker. Big
  parts round their edges in 1 or 2 steps a quarter turn (`mb_rbox`),
  small ones not at all; the tractor's wheels have 8 or 12 sides, the
  trailer's (under its body) 8. With the box trailer: 360 triangles at 0,
  922 at 1, 1,242 at 2. It is built again when the governor changes the
  preset.
- **Trees and hedges.** At MEDIUM and HIGH the trees are round: a smooth
  crown (an icosahedron's 20 faces, normals from its middle, darker
  underneath) on a five-sided trunk, about 30 triangles, and pines of two
  smooth seven-sided tiers; LOW keeps its box crowns and spires in its own
  run, at the same places, with the same collision boxes (the physics is
  the same at every detail). Hedges run along the fields' boundaries, where
  the field cuts find them (a cell's two stretches of one boundary made one,
  10,729 stretches): a smooth three-edged mound 1.5 m across and 1 m high,
  4 triangles, kept 8 m off the roads, a gap now and then; nothing collides
  with them, and LOW has none.
- **Houses** at MEDIUM and HIGH stand on LOW's boxes' footprints (the same
  collision boxes): cottages with walls to the eaves, a pitched roof with
  an overhang and gable ends, a chimney, a door and windows; the taller
  buildings in the middles of towns flat-roofed with a parapet and rows of
  windows; each about 30 to 60 triangles, flat-shaded, the walls darker at
  their feet. A depot's warehouse has an arched roof. Their contact shadows
  take the ground's own colour, darker. A town's streets are laid in 16 m
  pieces: one quad from end to end went to the chunk holding its middle and
  was drawn only while that chunk was in view, at every detail.
- **Depots** have each bay's number painted in it (between its lines, near
  the dock, where the reversing camera sees it) and over its door, and the
  depot's own number on the warehouse's face, in the HUD's font, at every
  detail; at MEDIUM and HIGH a hedge round the yard, open at the gate, and
  lamps at the apron's corners.
- **Verges and towns:** at MEDIUM and HIGH, bushes along the roads (1,095,
  one or two round blobs 7.5 to 10 m from the road's middle, off the
  places' grass and the other roads); at HIGH alone, lamp posts along the
  towns' streets every 48 m. Nothing collides with them, so the drive is
  the same at every detail.
- **Roads** at MEDIUM and HIGH have a white line along each edge; at every
  detail the centre's dashes (and the edge lines) stop 25 m short of a
  town's middle, where the roads meet and run over one another.
- **The camera** follows speed (the owner's idea): 64 degrees down for
  manoeuvring, easing down from 18 km/h to the preset's `pitch_min` by 61
  km/h (LOW 52, MEDIUM 24, HIGH 20 degrees), closer and looking a little
  higher as it comes down, back up quickly when the rig slows (coupling
  and docking keep their 75-degree view). At MEDIUM and HIGH the horizon
  comes into the picture: the sky is a gradient above it (sky.c), and a
  haze in the horizon's colour, drawn on the screen over the rows where the
  ground lies from half the far plane to 0.95 of it (and a little way up
  into the sky), ends the world as fog would. GL fog stays a preset's
  choice (`fog` in BUDGET.CFG) for a card that fogs for nothing: DOS-GL
  cuts a fogged triangle up until one fog factor a triangle follows GL's
  per-pixel fog, and on the G400 that made MEDIUM's 1,583 triangles a
  frame 8,056 and its 72.7 frames a second 21.7 (2026-11-20). LOW never
  comes low enough for either. The world is drawn as far round the target as
  the camera sees, and no less than 0.65 of the far plane; so LOW's lower
  view costs nothing (FW1 1,448 triangles a frame, against 1,440).
- **Light** on the world is baked (a warm sun from the north-west, strong
  ambient). The lorry keeps each vertex's normal and is lit again as it
  turns (`mb_relight`: the sun stays put in the world), with a plastic
  toy's glint where its surfaces face halfway between the sun and straight
  up. DOS-GL has no OpenGL lighting; this is a few hundred dot products a
  frame.

## The HUD and the menus (game/src/ui.c, hud.c, menu.c, garage.c)

Cartoon like the world: deep blue rounded panels with a soft shadow and a
light rim (`ui_panel`), the pixel font on them as before. A round
speedometer sits bottom left: the revs as a green arc round its edge (red
past 1,900 rpm), a needle for the speed (0 to 100 km/h over 240 degrees),
the speed in big figures, the gear in a gold medallion on its rim, the
damage as a rounded bar beside it. The money shows in a pill by a gold coin;
the minimap has a white rim in a deep blue ring. In the title, pause,
options and garage screens the chosen line sits on a lighter blue pill
rimmed in gold, its text gold (the font's dark outline reads badly on a
light pill); the garage's item card is rimmed in its rarity's colour.

The HUD's triangles cost the CPU what the world's do (perf.md), and drawn
as polygons the round parts came to 610 a frame, half again LOW's world.
So `ui_init` draws them once, antialiased, into a 256 x 256 texture: the
dial (its shadow, rim, face, ticks and the revs' track), the minimap's
frame (its translucent disc in the white and blue rim), the coin, the
gear's medallion and a white disc. Each is one textured quad; a panel's
corners are quarters of the disc (7 quads a layer, 3 for a pill), tinted by
the vertex colour. Only what moves is geometry: the revs' arc (up to 12
segments), the needle. The HUD is about 92 triangles a frame, 40 draws,
and every shape counts its triangles in FW-STAT.

## Shadows (game/src/lorry.c)

The tractor, its trailer and the trailers standing at depots each cast a
soft contact shadow, a little to the south-east as the buildings' baked
ones fall (the sun is in the north-west): a dark footprint fading out over
0.8 m, 10 triangles each, in one immediate-mode block before the lorries.
The lorry rides the terrain, and a road can stand well above it (its ribbon
is lifted a little, and more where the ground under it dips), so the
shadows are drawn with depth testing off, over whatever is there, and the
lorries then over them. With a trailer, the tractor's shadow starts at the
trailer's nose, so the two do not darken each other where they overlap.

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
- **Where to go:** before coupling, a gold arrow spins and bobs over the
  waiting trailer's nose; within 150 m of the bay, over the bay, and while
  docking a gold outline pulses round the bay on the ground (guides.c).
- **The route:** the shortest way over the road graph (Dijkstra). The HUD's
  arrow points along it and the minimap draws it; more than 60 m off it,
  it is worked out again from the nearest junction. On the road ahead, gold
  chevrons point the way (`route_draw`, guides.c): one at each road point
  from 7 m to 48 m from the rig, sliding forward and fading in and out by
  distance, drawn over the world like the shadows.
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

Roads that leave a town within 25 degrees of one another share a trunk: a
road out to a junction along their mean direction (up to 260 m, and under
half the way to the nearest of them), and from there one to each. Seed 1
has three, all Y-forks: town 1's roads to depot 6 and town 4, its roads
to depots 9 and 10, town 4's to depots 5 and 12. A junction is a node of
its own (kind 2, no place); its roads meet it level for 10 m.

In each town's middle, where its roads had all met at one point across the
streets, there is a roundabout: a ring 9 m wide, its centre line 24 m out
(`WG_RING_R`), round an island kerbed red and white, with grass, a tree and
a ring of bushes on it (a block of a tree at LOW). The island is solid to
its kerb: four collision boxes whose union is an octagon, its corners at
the kerb's 18 m. The town's roads stop at the ring, and so do the two
streets through the middle; their paint stops 6 m short. A route through
a town goes round the ring clockwise, as in Britain (`wg_ring_arc`, wgen.h):
the tour in fwgen, a job's route in jobs.c; the minimap draws the rings.
The trailer's axle runs 1.8 m inside the tractor's path on a 24 m ring, so
a rig on the ring's centre line clears the kerb by about 2 m.

The tour is the longest cycle through the network, starting at its town
nearest the map's middle (town 0, where a new game starts): before, the
start was whichever town's sum of the same cycle's lengths rounded
longest, and moving one road moved the start. The tour autopilot slows to
5 m/s where the path turns more than 80 degrees in the next 10 points: a
roundabout or a town's hairpin.

Towns and depots stand on flat ground: a town is levelled to its height
36 m beyond its outer streets (its blocks too), easing back into the hills
over 56 m, so no slope at a town's edge passes 25 degrees; a depot's yard
is levelled around its apron. On a place's flat ground a road is at the
place's height, easing back to its own smoothed profile over 120 m of road,
so roads meet in a town at one height and reach a gate at the apron's.

Nothing laid flat on the ground (roads, streets, aprons) has terrain above
it. Levelling flattens the 16 m height samples near a road, but a cell's
far corners could keep a crest's height, and the cell's triangles then rose
through the road between them (2026-11-19: 6% of the roads' points, and
whole stretches of the autopilot's tour and of the towns' streets, were
buried). Each terrain triangle under such a surface now has its corners
fitted to the surface's plane (its grade carried on along it, level
across): raised to no more than 0.3 m below it, then, for every surface
after every raise, cut to 2 cm below it. The physics is flat, so none of
this changes a drive. Roads that left a town side by side, each at its own
height, cut the ground from under one another, and stood up to 1.7 m
above it (2,769 samples over 1.2 m); with the trunks the most is 0.9 m,
none over 1.2.
