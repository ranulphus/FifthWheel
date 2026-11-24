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

4 x 4 km from (0, 0) to (4096, 4096), in 64 x 64 chunks of 64 m (version 2;
version 1 had 32 x 32 of 128 m: a chunk is culled whole, and the larger ones
sent DOS-GL several times the ground in view).
`make data-check` requires seed 1 to give `data/golden/world.sha`'s hash
when the generator is built at -O0 and at -O2.

| Section | Layout |
|---|---|
| `WRLD` | u32 version (2), seed, chunks per side (64), heightfield samples per side (257); float world size, chunk size, heightfield spacing (16 m), vertex units per metre (32) |
| `CIDX` | per chunk, row-major from the south-west: u32 first vertex, vertices, first index, triangles; s16 lowest and highest height (decimetres) |
| `CVRT` | vertices: s16 x, y, z, pad (chunk-local GL coordinates, 32 per metre: x - chunk x, height, chunk y - y), u8 r, g, b, a (lighting baked in) |
| `CTRI` | u16 indices, chunk-local |
| `CVAR` | per chunk: u32 LOW's triangles, MEDIUM and HIGH's, HIGH's: a chunk's triangles are four runs one after another, the first drawn only at LOW (the detail's `models` 0), then those every detail draws, then MEDIUM and HIGH's (`models` 1 and 2), then HIGH's alone (`models` 2); each detail draws one stretch of them; without the section every detail draws every triangle |
| `HGHT` | s16 heights (decimetres), 257 x 257 at 16 m: the terrain's own (the game reads the ground with the terrain triangles' split) |
| `FWCO` | collision boxes, as in the yard (buildings, docks, tree trunks, the world's edge) |
| `GRPH` | u32 nodes, edges, points; nodes {float x, y; u32 kind (0 town, 1 depot, 2 junction), place (a junction's 0xFFFFFFFF)}, at most `WG_MAX_NODES` (64); edges {u32 a, b, first point, points; float length}; points {float x, y} (8 m apart); every depot is a dead end (one road, from its gate); a road at a town starts or ends on its roundabout's ring (`WG_RING_R`, 24 m from the node), and a route through the town goes round the ring clockwise between them (`wg_ring_arc`) |
| `FWDP` | u32 count; depots {float x, y, heading; bays[3] {float x, y, heading}: a docked trailer's rear; pickup {float x, y, heading}: a waiting trailer's kingpin (the apron's left edge); u32 road node} |
| `FWPA` | the autopilot's tour: the longest loop of the road network that avoids the depots' dead ends |
| `FWSP` | the start, on the tour |

### HIGH's textured world (`build/data/WORLDHI.PAK`; game/src/wgen.h)

Made in the same run as WORLD.PAK (`fwgen SEED WORLD.PAK WORLDHI.PAK`), and
loaded only for HIGH (the game looks for the pack's name with `HI` before
its extension; without it, or not made with that WORLD.PAK, HIGH draws
untextured). `make data-check` holds it to `data/golden/worldhi.sha`.

| Section | Layout |
|---|---|
| `HWLD` | u32 version (1), seed, WORLD.PAK's vertex and triangle counts (its check) |
| `HIDX` | as `CIDX`, per chunk: HIGH's triangles (the shared, MEDIUM-and-HIGH and HIGH-only runs as one) |
| `HVRT` | 16 bytes a vertex: s16 x, y, z (as `CVRT`), s16 s, t (texels of the atlas: the texture matrix scales them by 1/512), u8 r, g, b, a, s16 pad; welded on all of them |
| `HTRI` | u16 x 3 a triangle, chunk-local |

The atlas (game/src/tex.c, drawn at start-up): 512 x 512, tiles of 128 in
rows (wgen.h's `WG_TX_*`), a face mapping one tile inside a 4-texel margin.

## Replays (`DGKR`, dgk/replay.h)

Header (16 bytes): `"DGKR"`, u16 version (1), u16 frame size, u32 ticks,
u32 seed; then one frame per tick, and after every 60th tick a u32 hash of
the game's state. Fifth Wheel's frame is 4 bytes: s8 steer, u8 accelerator,
u8 brake, u8 buttons (bit 0 handbrake, bits 1-2 a job taken from the board
(1-3), bit 3 the job cancelled, bit 4 the horn held). Playback checks the hashes and
reports the first second that differs (`FW-DESYNC`). A replay plays back
on the platform that recorded it: DOS's x87 and the desktop's SSE differ in
low-order bits, so hashes differ across them even when the drive does not.

## Saves (`dgk/save.h`) and the career

A save is a 16-byte header (u32 magic, version, data size, CRC-32 of the
data) and the data, written to a `.TMP` file beside it and then swapped
in (the old file removed first: DOS's rename does not replace). A reader
refuses another magic, version or size, or a CRC that does not match, and
takes up the `.TMP` file if a crash came between the removal and the
rename.

`CAREER.DAT`: magic `FWCR`, version 1, 52 bytes of u32s: money,
deliveries, perfect docks, metres hauled, a bit per garage item owned, the
item fitted of each kind (paint, horn, decal, licence, cab), three spare.

## Settings (`dgk/cfg.h`)

`FWHEEL.CFG`: `key = value` lines (`#` comments), saved the same way; the
joystick's map is `joy.steer.axis`, `.rest`, `.left`, `.right`,
`joy.accel.*`, `joy.brake.*`, `joy.deadzone` (thousandths), `joy.handbrake`
and `joy.horn` (buttons).
