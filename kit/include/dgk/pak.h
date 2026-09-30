/* dgk/pak.h - packs: one file of named, CRC-checked sections, loaded whole
 * into memory and used in place (little-endian, 16-byte aligned sections).
 *
 *   header   "DGKP", u16 major, u16 minor, u32 sections, u32 table offset,
 *            u32 file size, u32 CRC-32 of the section table, u32 seed,
 *            u32 generator build hash                               (32 bytes)
 *   table    per section: u32 fourcc, u32 offset, u32 size, u32 CRC-32
 *
 * A reader refuses another major version; a newer minor version may only
 * add sections. Sections' layouts belong to their users (docs/formats.md). */
#ifndef DGK_PAK_H
#define DGK_PAK_H

#include "dgk/base.h"

#define DGK_PAK_MAJOR 1
#define DGK_PAK_MINOR 0
#define DGK_FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

typedef struct dgk_pak {
    uint8_t *data;
    uint32_t size, nsections, seed;
    const uint32_t *table;
} dgk_pak;

int  dgk_pak_load(dgk_pak *p, const char *path);   /* 0, or -1 with a logged reason */
void dgk_pak_free(dgk_pak *p);
const void *dgk_pak_section(const dgk_pak *p, uint32_t fourcc, uint32_t *size);  /* NULL if absent */

#endif
