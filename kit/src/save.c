/* save.c - saves (see dgk/save.h). */
#include "dgk/save.h"
#include <stdio.h>
#include <string.h>

const char *dgk_temp_path(const char *path, char *buf, size_t n)
{
    char *dot;
    snprintf(buf, n, "%s", path);
    dot = strrchr(buf, '.');
    if (dot && !strchr(dot, '/') && !strchr(dot, '\\'))
        snprintf(dot, n - (size_t)(dot - buf), ".TMP");
    else
        snprintf(buf + strlen(buf), n - strlen(buf), ".TMP");
    return buf;
}

int dgk_replace(const char *tmp, const char *path)
{
    remove(path);
    return rename(tmp, path) == 0 ? 0 : -1;
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int dgk_save_write(const char *path, uint32_t magic, uint32_t version, const void *data, uint32_t size)
{
    char tmp[96];
    unsigned char head[16];
    FILE *f;
    int ok;
    put32(head, magic);
    put32(head + 4, version);
    put32(head + 8, size);
    put32(head + 12, dgk_crc32(0, data, size));
    f = fopen(dgk_temp_path(path, tmp, sizeof tmp), "wb");
    if (!f)
        return -1;
    ok = fwrite(head, 1, sizeof head, f) == sizeof head && fwrite(data, 1, size, f) == size;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        remove(tmp);
        return -1;
    }
    return dgk_replace(tmp, path);
}

static int read_one(const char *path, uint32_t magic, uint32_t version, void *data, uint32_t size)
{
    static unsigned char buf[4096];
    unsigned char head[16];
    FILE *f = fopen(path, "rb");
    int ok;
    if (!f)
        return DGK_SAVE_MISSING;
    ok = size <= sizeof buf && fread(head, 1, sizeof head, f) == sizeof head && get32(head) == magic &&
         get32(head + 4) == version && get32(head + 8) == size && fread(buf, 1, size, f) == size &&
         dgk_crc32(0, buf, size) == get32(head + 12);
    fclose(f);
    if (!ok)
        return DGK_SAVE_BAD;
    memcpy(data, buf, size);
    return DGK_SAVE_OK;
}

int dgk_save_read(const char *path, uint32_t magic, uint32_t version, void *data, uint32_t size)
{
    char tmp[96];
    int r = read_one(path, magic, version, data, size);
    if (r == DGK_SAVE_MISSING && read_one(dgk_temp_path(path, tmp, sizeof tmp), magic, version, data, size) ==
                                     DGK_SAVE_OK) {
        dgk_replace(tmp, path);                      /* the crash came between removing and renaming */
        return DGK_SAVE_OK;
    }
    return r;
}
