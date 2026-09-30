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

The test yard (`tools/fwyard.c`, `build/data/YARD.PAK`) is F1's world; the
generated world (below) has the same `FWCO`, `FWPA` and `FWSP`.

### The generated world (`tools/fwgen.c`, `build/data/WORLD.PAK`; game/src/wgen.h)

4 x 4 km from (0, 0) to (4096, 4096), in 32 x 32 chunks of 128 m.
`make data-check` requires seed 1 to give `data/golden/world.sha`'s hash
when the generator is built at -O0 and at -O2.

| Section | Layout |
|---|---|
| `WRLD` | u32 version (1), seed, chunks per side (32), heightfield samples per side (257); float world size, chunk size, heightfield spacing (16 m), vertex units per metre (32) |
| `CIDX` | per chunk, row-major from the south-west: u32 first vertex, vertices, first index, triangles; s16 lowest and highest height (decimetres) |
| `CVRT` | vertices: s16 x, y, z, pad (chunk-local GL coordinates, 32 per metre: x - chunk x, height, chunk y - y), u8 r, g, b, a (lighting baked in) |
| `CTRI` | u16 indices, chunk-local |
| `HGHT` | s16 heights (decimetres), 257 x 257 at 16 m: the terrain's own (the game reads the ground with the terrain triangles' split) |
| `FWCO` | collision boxes, as in the yard (buildings, docks, tree trunks, the world's edge) |
| `GRPH` | u32 nodes, edges, points; nodes {float x, y; u32 kind (0 town, 1 depot), place}; edges {u32 a, b, first point, points; float length}; points {float x, y} (8 m apart) |
| `FWDP` | u32 count; depots {float x, y, heading; bays[3] {float x, y, heading}: a docked trailer's rear; pickup {float x, y, heading}: a waiting trailer's kingpin; u32 road node} |
| `FWPA` | the autopilot's tour: the longest loop of the road network that avoids the depots' dead ends |
| `FWSP` | the start, on the tour |

## Replays (`DGKR`, dgk/replay.h)

Header (16 bytes): `"DGKR"`, u16 version (1), u16 frame size, u32 ticks,
u32 seed; then one frame per tick, and after every 60th tick a u32 hash of
the game's state. Fifth Wheel's frame is 4 bytes: s8 steer, u8 accelerator,
u8 brake, u8 buttons (bit 0 handbrake). Playback checks the hashes and
reports the first second that differs (`FW-DESYNC`). A replay plays back
on the platform that recorded it: DOS's x87 and the desktop's SSE differ in
low-order bits, so hashes differ across them even when the drive does not.
