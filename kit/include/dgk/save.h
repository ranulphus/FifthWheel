/* dgk/save.h - files a game keeps between runs. A save is a 16-byte header
 * (magic, version, size, CRC-32 of the data) and the data, written to a
 * temporary file beside it and then swapped in: DOS's rename does not
 * replace, so the old file goes first, and a crash at any point leaves the
 * old save or the new one (or, between the two, the temporary file, which
 * dgk_save_read then takes up). */
#ifndef DGK_SAVE_H
#define DGK_SAVE_H

#include "dgk/base.h"

enum { DGK_SAVE_OK = 0, DGK_SAVE_MISSING = -1, DGK_SAVE_BAD = -2 };

int dgk_save_write(const char *path, uint32_t magic, uint32_t version, const void *data, uint32_t size);
/* DGK_SAVE_OK and data filled; or MISSING, or BAD (another magic, version
 * or size, or a CRC that does not match), data untouched. */
int dgk_save_read(const char *path, uint32_t magic, uint32_t version, void *data, uint32_t size);

/* The temporary file's name beside path: the extension replaced by .TMP
 * (8.3: CAREER.DAT -> CAREER.TMP); and putting it in path's place. */
const char *dgk_temp_path(const char *path, char *buf, size_t n);
int dgk_replace(const char *tmp, const char *path);

#endif
