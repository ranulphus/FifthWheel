# File formats

## Packs (`DGKP`, dgk/pak.h)

One file of CRC-checked sections, loaded whole and used in place;
little-endian, sections 16-byte aligned. Header (32 bytes): `"DGKP"`, u16
major (1), u16 minor (0), u32 section count, u32 table offset, u32 file size,
u32 CRC-32 of the table, u32 seed, u32 generator build hash. Table entries:
u32 fourcc, u32 offset, u32 size, u32 CRC-32 of the section. A reader refuses
another major version; a newer minor may only add sections.

### Fifth Wheel's world sections (game/src/world.h)

Ground-plane coordinates: x east, y north, metres; GL is (x, height, -y).

| Section | Layout |
|---|---|
| `FWMS` | u32 vertices, u32 triangles, float pos[3 per vertex] (GL coordinates), u8 rgba[4 per vertex], u16 indices[3 per triangle] |
| `FWCO` | u32 count, then boxes {float cx, cy, half length, half width, angle} |
| `FWPA` | u32 count, then points {float x, y}: a closed loop (the autopilot's) |
| `FWSP` | float x, y, heading: the start |

The test yard (`tools/fwyard.c`, `build/data/YARD.PAK`) is F1's world; F3's
generator replaces these with chunked sections.

## Replays (`DGKR`, dgk/replay.h)

Header (16 bytes): `"DGKR"`, u16 version (1), u16 frame size, u32 ticks,
u32 seed; then one frame per tick, and after every 60th tick a u32 hash of
the game's state. Fifth Wheel's frame is 4 bytes: s8 steer, u8 accelerator,
u8 brake, u8 buttons (bit 0 handbrake). Playback checks the hashes and
reports the first second that differs (`FW-DESYNC`). A replay plays back
on the platform that recorded it: DOS's x87 and the desktop's SSE differ in
low-order bits, so hashes differ across them even when the drive does not.
