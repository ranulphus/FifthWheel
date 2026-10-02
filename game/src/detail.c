/* detail.c - detail presets and the governor (see detail.h). */
#include "detail.h"
#include "dgk/cfg.h"
#include "dgk/log.h"
#include "fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const detail_name[DETAILS] = { "low", "medium", "high" };

static const detail defaults[DETAILS] = {
    { 110.0f, 1, 8, 24, 12, 4, 110.0f },
    { 140.0f, 2, 12, 48, 24, 3, 160.0f },
    { 180.0f, 2, 16, 64, 32, 2, 220.0f },
};

static float num(const dgk_cfg *c, const char *preset, const char *key, float fallback)
{
    char k[32];
    const char *v;
    snprintf(k, sizeof k, "%s.%s", preset, key);
    v = dgk_cfg_get(c, k);
    return v ? (float)atof(v) : fallback;
}

void detail_load(detail presets[DETAILS], const char *budget_path)
{
    static dgk_cfg c;
    int i, read = budget_path && dgk_cfg_load(&c, budget_path) == 0;
    for (i = 0; i < DETAILS; i++) {
        detail *d = &presets[i];
        const char *n = detail_name[i];
        *d = defaults[i];
        if (!read)
            continue;
        d->far = DGK_CLAMP(num(&c, n, "far", d->far), 60.0f, 400.0f);
        d->zoom_max = (int)DGK_CLAMP(num(&c, n, "zoom_max", (float)d->zoom_max), 0.0f, 2.0f);
        d->coins = (int)DGK_CLAMP(num(&c, n, "coins", (float)d->coins), 0.0f, (float)FX_COINS);
        d->confetti = (int)DGK_CLAMP(num(&c, n, "confetti", (float)d->confetti), 0.0f, (float)FX_CONFETTI);
        d->dust = (int)DGK_CLAMP(num(&c, n, "dust", (float)d->dust), 0.0f, (float)FX_DUST);
        d->minimap_step = (int)DGK_CLAMP(num(&c, n, "minimap_step", (float)d->minimap_step), 1.0f, 8.0f);
        d->trailers = DGK_CLAMP(num(&c, n, "trailers", d->trailers), 40.0f, 400.0f);
    }
    if (read)
        dgk_log("FW-CFG budget %s: low far %.0f, medium far %.0f, high far %.0f", budget_path, presets[0].far,
                presets[1].far, presets[2].far);
}

int detail_parse(const char *name)
{
    int i;
    for (i = 0; name && i < DETAILS; i++)
        if (!strcmp(name, detail_name[i]))
            return i;
    return -1;
}

detail detail_effective(const detail presets[DETAILS], int chosen, int notch)
{
    int p = chosen - notch, k;
    detail d = presets[DGK_MAX(p, DETAIL_LOW)];
    for (k = p; k < DETAIL_LOW; k++)                 /* below LOW: the far plane shrinks */
        d.far *= 0.8f;
    return d;
}

int governor_frame(governor *g, float frame_ms)
{
    if (!g->on)
        return 0;
    g->avg_ms = g->avg_ms > 0 ? g->avg_ms + (frame_ms - g->avg_ms) * 0.1f : frame_ms;
    g->slow_ms = g->avg_ms > 36.0f ? g->slow_ms + frame_ms : 0;
    g->fast_ms = g->avg_ms < 25.0f ? g->fast_ms + frame_ms : 0;
    if (g->slow_ms > 2000.0f && g->notch < 4) {
        g->notch++;
        g->slow_ms = 0;
        dgk_log("FW-GOV down to notch %d (%.1f ms a frame)", g->notch, g->avg_ms);
        return 1;
    }
    if (g->fast_ms > 6000.0f && g->notch > 0) {
        g->notch--;
        g->fast_ms = 0;
        dgk_log("FW-GOV up to notch %d (%.1f ms a frame)", g->notch, g->avg_ms);
        return 1;
    }
    return 0;
}
