# FifthWheel
Juggernaut 3D for DOS-GL

A top-down 3D, low-poly articulated-lorry delivery game for MS-DOS on Matrox
G200/G400/G450 cards: hitch a trailer at a depot, haul it across a
procedurally generated 4 x 4 km world, reverse it into a loading bay. The
look is light-hearted rather than realistic: Transport Tycoon's toy world,
Ignition's chunky vehicles, Fortnite's colour and bounce.

It runs on [DOS-GL](https://github.com/ranulphus/DOSGL) (OpenGL 1.1 on
Matrox cards under DJGPP) through SDL3, and is the example game for **dgk**,
the DOS-GL Kit: the game-agnostic layer in `kit/` (main loop, platform,
drawing within DOS-GL's subset, text, mixer, test hooks), which will become
its own repository once a second game uses it.

Minimum machine: Pentium II 266, Matrox G200, Sound Blaster 16 (or none),
640x480. Faster machines and the G400/G450 get more detail.

**Status:** milestone F8: hardening and a release ZIP (`make release`;
`docs/bench.md` lists what waits on real machines). F7: a title screen, a pause menu and options
(detail, screen size, vsync, volume), three detail presets with a
governor, and the world culled to what the camera sees. Before that, F6: a career. Money earned is kept in CAREER.DAT and
spent in the garage on paint jobs, horns, decals, licences for flatbeds
and tankers, and a bigger cab; deliveries end in coins, confetti and a
bouncing callout. Before that (F4, F5): the jobs loop, full sound, and
joysticks and wheels. Take a job from a depot's board,
back under its trailer (box, flatbed or tanker), follow the route on the
minimap to another depot and reverse the trailer into a bay, watching a
reversing camera's guide lines; the dock is graded and paid. The world is generated (`make data`: towns, depots with
loading bays, a road network levelled into gentle hills, patchwork fields,
woods) and drawn chunk by chunk, the lorry riding its slopes. The game
runs on DOS, Linux and headless (kinematic tractor and trailer, a six-speed
automatic, collisions, synthesised sounds, autopilots, checked replays),
and DOSBench times it (FW1, FWP).

## Building

Needs a DOSGL checkout with SDL3 built (`make sdl sdl-host` there; its path in
`config.mk` or `config.local.mk`) and the DJGPP kit (`make setup-djgpp` in
DOSGL).

```
make dos          # build/dos/FWHEEL.EXE (ship CWSDPMI.EXE beside it)
make linux        # build/linux/fwheel (SDL3 + desktop OpenGL; built in the dev container)
make headless     # build/headless/fwheel-hl (OSMesa, for tests)
make loopa CARD=g450              # FWHEEL.EXE in 86Box (DOSGL's harness)
make jobsweep                     # the job autopilot on every job, headless
make tests-host                   # unit tests (no screen) and the GL-subset check
make suite CARD=g450              # Loop A: sound, joystick, shop, menus, exits and crashes, memory,
                                  # screen sizes, cards agreeing, the release ZIP
make release                      # dist/fwheel-ID.zip: the game, CWSDPMI, README.TXT
make winvm CARD=g450              # dist/fwheel-g450-vm.zip: a ready-to-boot 86Box machine with it installed
                                  # (MGA-Glide's patched 86Box; type FW at the prompt)
```

Playing starts at the title screen; Esc pauses. Keys: arrows or WASD to steer, accelerate and brake (hold the brake at a
standstill to reverse), Space for the handbrake, H for the horn, 1-3 to take a job from a
depot's board, Backspace to cancel it before coupling, G for the garage
(paint, horns, decals, licences, a bigger cab), Z to zoom, J to set up a
joystick or wheel, Esc to quit. With a joystick: its steering, accelerator
and brake as set up, button 1 the handbrake, button 2 the horn.

The world comes from a seed: `make data WORLD_SEED=n` (default 1;
`make data-check` checks seed 1 against `data/golden/world.sha`).

Command line: `-mode WxH`, `-novsync`, `-nosound`, and for tests `-frames N`,
`-fixed` (one tick per frame), `-shot F:NAME`, `-nodraw`, `-test` (see
`kit/include/dgk/app.h`); the game adds `-world FILE` (default
`WORLD.PAK`; the F1 test yard is `YARD.PAK`), `-autopilot` (`-laps N`),
`-autojob` (`-job D:T:B`), `-dockpose`, `-soundtest`, `-calibrate`, `-joylog`, `-career FILE`, `-money N`, `-fxtest`,
`-detail low|medium|high`, `-zoom N`, `-title`,
`-record FILE`, `-replay FILE`, `-hash`, `-trace N`, `-timedemo FILE -dbtest NAME`,
`-probe [quick]` and `-tourshots N`.

## Licence

MIT (`LICENSE`). Every asset is generated from this repository (the font
from `data/font5x7.txt`).
