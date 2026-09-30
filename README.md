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

**Status:** milestone F0: the kit's first scene runs on DOS (86Box, G200 and
G450), on Linux and headless, with the same pictures on all three.

## Building

Needs a DOSGL checkout with SDL3 built (`make sdl sdl-host` there; its path in
`config.mk` or `config.local.mk`) and the DJGPP kit (`make setup-djgpp` in
DOSGL).

```
make dos          # build/dos/FWHEEL.EXE (ship CWSDPMI.EXE beside it)
make linux        # build/linux/fwheel (SDL3 + desktop OpenGL; built in the dev container)
make headless     # build/headless/fwheel-hl (OSMesa, for tests)
make loopa CARD=g450              # FWHEEL.EXE in 86Box (DOSGL's harness)
```

Command line: `-mode WxH`, `-novsync`, `-nosound`, and for tests `-frames N`,
`-fixed` (one tick per frame), `-shot F:NAME`, `-test` (see
`kit/include/dgk/app.h`). F0 adds `-tone` (a 440 Hz test tone).

## Licence

MIT (`LICENSE`). Every asset is generated from this repository (the font
from `data/font5x7.txt`).
