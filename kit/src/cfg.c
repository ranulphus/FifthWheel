/* cfg.c - settings files (see dgk/cfg.h). */
#include "dgk/cfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = 0;
    return s;
}

int dgk_cfg_load(dgk_cfg *c, const char *path)
{
    char line[128];
    FILE *f = fopen(path, "r");
    c->n = 0;
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='), *k;
        if (line[0] == '#' || !eq)
            continue;
        *eq = 0;
        k = trim(line);
        if (*k)
            dgk_cfg_set(c, k, trim(eq + 1));
    }
    fclose(f);
    return 0;
}

int dgk_cfg_save(const dgk_cfg *c, const char *path)
{
    char tmp[96];
    char *dot;
    FILE *f;
    int i, ok;
    snprintf(tmp, sizeof tmp, "%s", path);
    dot = strrchr(tmp, '.');
    if (dot && !strchr(dot, '/') && !strchr(dot, '\\'))
        snprintf(dot, sizeof tmp - (size_t)(dot - tmp), ".TMP");   /* 8.3: FWHEEL.CFG -> FWHEEL.TMP */
    else
        snprintf(tmp + strlen(tmp), sizeof tmp - strlen(tmp), ".TMP");
    f = fopen(tmp, "w");
    if (!f)
        return -1;
    for (i = 0; i < c->n; i++)
        fprintf(f, "%s = %s\n", c->key[i], c->value[i]);
    ok = fclose(f) == 0;
    if (!ok) {
        remove(tmp);
        return -1;
    }
    remove(path);
    return rename(tmp, path) == 0 ? 0 : -1;
}

const char *dgk_cfg_get(const dgk_cfg *c, const char *key)
{
    int i;
    for (i = 0; i < c->n; i++)
        if (!strcmp(c->key[i], key))
            return c->value[i];
    return NULL;
}

int dgk_cfg_int(const dgk_cfg *c, const char *key, int fallback)
{
    const char *v = dgk_cfg_get(c, key);
    return v ? atoi(v) : fallback;
}

void dgk_cfg_set(dgk_cfg *c, const char *key, const char *value)
{
    int i;
    for (i = 0; i < c->n && strcmp(c->key[i], key); i++)
        continue;
    if (i == c->n) {
        if (c->n == DGK_CFG_MAX)
            return;
        c->n++;
        snprintf(c->key[i], sizeof c->key[i], "%s", key);
    }
    snprintf(c->value[i], sizeof c->value[i], "%s", value);
}

void dgk_cfg_set_int(dgk_cfg *c, const char *key, int value)
{
    char v[16];
    snprintf(v, sizeof v, "%d", value);
    dgk_cfg_set(c, key, v);
}
