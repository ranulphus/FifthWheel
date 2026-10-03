# The bench checklist

What 86Box cannot show, to check on real machines: the minimum one (a
Pentium II 266 with a Matrox G200, an ISA Sound Blaster 16 or AWE, a
gameport joystick), then the G400 and the G450. Run from the release ZIP
(`make release`), unpacked alone in a directory.

## The G200's edge registers (fixed in DOS-GL 06def8b)

On devserver's G200eR2 (2026-10-02, MGA-Glide `docs/loop-c-results.md`) the
G200's edge registers (AR0-AR6, 18 bits) overflowed for triangles more than
about 768 rows tall, which drew short. Fifth Wheel has such triangles: the
HUD's full-height panels at 1024x768 and 1280x1024, and ground near the
camera that reaches far off the screen (DOS-GL clips only at its
2,000-pixel guard band). The HAL now divides the edge terms' common factor
of 16 out, so edges up to 8,191 pixels fit with the same pixels (MGA-Glide
`d4c289a`, synced into DOS-GL `06def8b`, which `deps.mk` pins), and 86Box
models the 18-bit fields on the G100 and G200 (MGA-Glide's patch 0012), so
Loop A shows the overflow if it comes back. Bench with a build at or after
that pin.

## Speed (DOSBench, `tools/run.py games --tests FW1,FW1L,FW1H,FWP`)

- [ ] `FW1L` (LOW) averages 30 fps or more with p99 at or under 45 ms on
      the PII-266 with the G200. If not: the `FWP` records fit the frame
      time model (`docs/perf.md`), `BUDGET.CFG`'s LOW numbers come down,
      and the shortfall's cause is written down.
- [ ] `FW1` (MEDIUM) and `FW1H` (HIGH, zoomed out) on the same machine and
      on the G400 and G450 machines: which preset each should default to.
- [ ] The governor (on by default when playing): driving into a town on
      the PII-266 steps down without a visible stutter, and steps back up.
- [ ] Each screen size in the options, on each card: the picture fills the
      screen as expected (scaled and zoomed sizes, 320x200 stretched).

## Sound

- [ ] The SB16 and an AWE: no crackle or gaps while driving, in towns, in
      the garage and on the title screen; the engine note follows the revs.
- [ ] Starting a job, coupling, delivering (coins, chime): no gaps.
- [ ] BLASTER with the wrong IRQ: the game says why in its log and runs
      silent, and quits normally.

## Joystick and wheel

- [ ] The set-up (J) with a 2-axis stick and, if there is one, a wheel
      with pedals: each control found, saved, and right after a restart.
- [ ] A long drive with the stick: the centre does not drift (the
      gameport's timing on a real machine).
- [ ] Steering feel: the stick's dead zone (`joy.deadzone` in FWHEEL.CFG,
      thousandths) is neither twitchy nor sluggish.

## Leaving the machine usable

- [ ] Quitting from the menu, Ctrl-C and Ctrl-Break: back at the prompt,
      the keyboard typing, no sound looping.
- [ ] After a crash (`SET FW_CRASH=300`, then `FWHEEL`): text mode, the
      keyboard typing, no sound looping; `CAREER.DAT` as it was.

## Memory

- [ ] A 32 MB machine: the game starts and plays (86Box with 32 MB: the
      heap is about 7 MB, 21 MB left free).
- [ ] Under EMM386 or another memory manager, and in a Windows 9x DOS box
      if it should work there at all: note what happens.

## Pictures on silicon

- [ ] `make shots`' poses on the G200, G400 and G450 against Mesa (DOS-GL's
      conformance on silicon covers the drawing itself; this covers the
      game's own use of it).
