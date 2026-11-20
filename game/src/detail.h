/* detail.h - the detail presets (LOW, MEDIUM, HIGH): how far the camera may
 * pull back and see, how many flourishes play at once, how much of the
 * road network the minimap draws, how far away standing trailers are
 * drawn, how round the models are. Their numbers come from BUDGET.CFG when it is there (`low.far =
 * 110`, ...), so the bench's measurements can retune them without a new
 * build. The governor steps below the chosen preset while frames run slow
 * (off in tests). */
#ifndef FW_DETAIL_H
#define FW_DETAIL_H

#include "dgk/base.h"

enum { DETAIL_LOW, DETAIL_MEDIUM, DETAIL_HIGH, DETAILS };

typedef struct detail {
    float far;                  /* metres: the far plane, and the world's reach round the target */
    int zoom_max;               /* the furthest zoom allowed: 0 near, 1 normal, 2 far */
    int coins, confetti, dust;  /* flourishes at once (at most fx's pools) */
    int minimap_step;           /* road points one minimap segment spans */
    float trailers;             /* metres within which standing trailers are drawn */
    int models;                 /* how round the models are: 0 boxes, 1 rounded, 2 rounder */
    float pitch_min;            /* degrees down: how low the camera comes at speed (under 30: sky and fog) */
    int fog;                    /* GL fog when the camera sees that far (0: sky.c's haze on the screen instead) */
} detail;

extern const char *const detail_name[DETAILS];

/* The compiled-in presets, then BUDGET.CFG's numbers over them (if found). */
void detail_load(detail presets[DETAILS], const char *budget_path);
int  detail_parse(const char *name);          /* "low", "medium", "high"; -1 if neither */
/* The preset a card is meant for (the owner's targets, 2026-11-19): LOW
 * for the G200 (and the G100, the software renderer, anything unknown on
 * DOS-GL), MEDIUM for the G400, G450 and the TNT2, HIGH for the Radeon 7200
 * and 7500 (when DOS-GL drives them) and a desktop's own OpenGL. From
 * glGetString(GL_RENDERER): "DOS-GL on MGA-G450" and the like. */
int  detail_for_renderer(const char *renderer);

/* The governor: fed each frame's milliseconds, it steps down a notch after
 * 2 s averaging slower than 36 ms (under 28 fps) and back up after 6 s
 * faster than 25 ms. Each notch is a preset lower; below LOW, the far
 * plane shrinks a fifth a notch. */
typedef struct governor {
    int on, notch;
    float avg_ms, slow_ms, fast_ms;
} governor;

int    governor_frame(governor *g, float frame_ms);   /* 1 when the notch changed */
detail detail_effective(const detail presets[DETAILS], int chosen, int notch);

#endif
