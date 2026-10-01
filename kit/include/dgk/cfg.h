/* dgk/cfg.h - a settings file: one `key = value` per line, `#` comments.
 * Saving writes a temporary file beside it and then replaces the old one
 * (on DOS rename() does not overwrite, so the old one is removed first):
 * a crash leaves the old file or the new one, never half of one. */
#ifndef DGK_CFG_H
#define DGK_CFG_H

#include "dgk/base.h"

#define DGK_CFG_MAX 48

typedef struct dgk_cfg {
    int n;
    char key[DGK_CFG_MAX][32];
    char value[DGK_CFG_MAX][64];
} dgk_cfg;

int  dgk_cfg_load(dgk_cfg *c, const char *path);    /* 0 if read; c is emptied first either way */
int  dgk_cfg_save(const dgk_cfg *c, const char *path);
const char *dgk_cfg_get(const dgk_cfg *c, const char *key);   /* NULL if unset */
int  dgk_cfg_int(const dgk_cfg *c, const char *key, int fallback);
void dgk_cfg_set(dgk_cfg *c, const char *key, const char *value);
void dgk_cfg_set_int(dgk_cfg *c, const char *key, int value);

#endif
