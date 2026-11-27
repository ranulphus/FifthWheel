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
    { 110.0f, 0.0f, 1, 8, 24, 12, 4, 110.0f, 0, 52.0f, 0 },
    { 140.0f, 0.0f, 2, 12, 48, 24, 3, 160.0f, 1, 24.0f, 0 },
    { 180.0f, 400.0f, 2, 16, 64, 32, 2, 220.0f, 2, 20.0f, 2 },
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
        d->far_engine = num(&c, n, "far_engine", d->far_engine);
        d->far_engine = d->far_engine > 0.0f ? DGK_CLAMP(d->far_engine, 60.0f, 400.0f) : 0.0f;
        d->zoom_max = (int)DGK_CLAMP(num(&c, n, "zoom_max", (float)d->zoom_max), 0.0f, 2.0f);
        d->coins = (int)DGK_CLAMP(num(&c, n, "coins", (float)d->coins), 0.0f, (float)FX_COINS);
        d->confetti = (int)DGK_CLAMP(num(&c, n, "confetti", (float)d->confetti), 0.0f, (float)FX_CONFETTI);
        d->dust = (int)DGK_CLAMP(num(&c, n, "dust", (float)d->dust), 0.0f, (float)FX_DUST);
        d->minimap_step = (int)DGK_CLAMP(num(&c, n, "minimap_step", (float)d->minimap_step), 1.0f, 8.0f);
        d->trailers = DGK_CLAMP(num(&c, n, "trailers", d->trailers), 40.0f, 400.0f);
        d->models = (int)DGK_CLAMP(num(&c, n, "models", (float)d->models), 0.0f, 2.0f);
        d->pitch_min = DGK_CLAMP(num(&c, n, "pitch_min", d->pitch_min), 15.0f, 64.0f);
        d->fog = (int)DGK_CLAMP(num(&c, n, "fog", (float)d->fog), 0.0f, 2.0f);
    }
    if (read)
        dgk_log("FW-CFG budget %s: low far %.0f, medium far %.0f, high far %.0f (%.0f on the engine)", budget_path,
                presets[0].far, presets[1].far, presets[2].far, detail_far(&presets[2], 1));
}

static int has(const char *s, const char *word)
{
    size_t n = strlen(word);
    for (; *s; s++) {
        size_t i;
        for (i = 0; i < n && s[i] && (s[i] | 32) == (word[i] | 32); i++)
            continue;
        if (i == n)
            return 1;
    }
    return 0;
}

int detail_for_renderer(const char *renderer)
{
    if (!renderer)
        return DETAIL_LOW;
    if (has(renderer, "RV100"))                     /* the Radeon 7000/VE: no transform engine */
        return DETAIL_MEDIUM;
    if (has(renderer, "radeon"))
        return DETAIL_HIGH;
    /* The Quadro2 MXR (NV11): DOS-GL's transform engine on its Celsius (RetroGPU 37f1e44; the owner, 2026-11-27).
     * The NV11's GeForce2 names stay unknown (LOW) until one is seen working there. */
    if (has(renderer, "Quadro2"))
        return DETAIL_HIGH;
    if (has(renderer, "G400") || has(renderer, "G450") || has(renderer, "G550") || has(renderer, "TNT"))
        return DETAIL_MEDIUM;
    if (strncmp(renderer, "DOS-GL", 6) != 0)
        return DETAIL_HIGH;                          /* a desktop's own OpenGL (the Linux build) */
    return DETAIL_LOW;                               /* the G200, the G100, software, anything else */
}

int detail_engine(const char *extensions)
{
    return extensions && strstr(extensions, "GL_DGL_hw_tnl") != NULL;
}

int detail_fog(const detail *d, int engine)
{
    return d->fog == 1 || (d->fog == 2 && engine);
}

float detail_far(const detail *d, int engine)
{
    return engine && d->far_engine > 0.0f ? d->far_engine : d->far;
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
