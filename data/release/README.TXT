FIFTH WHEEL
===========

Deliveries for a toy lorry, for MS-DOS with a Matrox G200, G400 or G450.

Take a job at a depot, back the tractor under its trailer, follow the
route on the minimap to another depot and reverse the trailer into a bay.
The neater the dock, the bigger the pay. Spend it in the garage on paint,
horns, decals, licences for flatbeds and tankers, and a bigger cab.


WHAT IT NEEDS
-------------

* A Pentium II (266 MHz or faster) and 32 MB of memory.
* A Matrox G200, G400 or G450 graphics card (AGP or PCI).
* MS-DOS, FreeDOS or a DOS prompt without Windows running.
* Optional: a Sound Blaster 16, AWE32/64 or Sound Blaster Pro (or a
  compatible), and a joystick or wheel on the game port.

CWSDPMI.EXE comes with the game; keep it beside FWHEEL.EXE.


STARTING
--------

    SET BLASTER=A220 I5 D1 H5 T6        (your Sound Blaster's settings)
    FWHEEL

Without BLASTER the game runs silent. FWHEEL -nosound (in small letters)
does the same on purpose.


CONTROLS
--------

    Arrows or W A S D   steer, accelerate, brake
                        (hold the brake at a standstill to reverse)
    Space               handbrake
    H                   horn
    1, 2, 3             take a job from a depot's board
    Backspace           cancel the job (before coupling)
    G                   the garage (stopped)
    Z                   zoom the camera
    J                   joystick or wheel set-up
    Esc                 pause (resume, options, quit)

A joystick or wheel: set it up first with J (or JOYSTICK SET-UP on the
title screen). The set-up asks for the controls at rest, full left, full
right, full accelerator and full brake, and uses whichever axis moves, so
a stick and a wheel with pedals both work. Button 1 is the handbrake,
button 2 the horn.


OPTIONS
-------

DETAIL       LOW, MEDIUM or HIGH: how far you see and how many flourishes.
SCREEN       the screen size, from the next start.
VSYNC        wait for the screen's refresh (smoother; turn off if slow).
VOLUME       0 to 10.
GOVERNOR     lowers the detail by itself while frames run slow.

The options, and the joystick's set-up, are kept in FWHEEL.CFG. Your
money and the garage's items are kept in CAREER.DAT; delete it to start
a new career.


FILES
-----

    FWHEEL.EXE     the game
    WORLD.PAK      the world (4 x 4 km of towns, depots and roads)
    BUDGET.CFG     the detail presets' numbers
    CWSDPMI.EXE    the DPMI host the game needs under plain DOS
    CWSDPMI.DOC    its documentation
    README.TXT     this file
    LICENSE.TXT    the game's licence (MIT)
    THIRDPTY.TXT   the licences of the libraries in FWHEEL.EXE


Fifth Wheel is free software (MIT licence). Its source, and the
DOS-GL graphics library it runs on, are at
https://github.com/ranulphus/FifthWheel and
https://github.com/ranulphus/DOSGL
