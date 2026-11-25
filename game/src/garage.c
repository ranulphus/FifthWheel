/* garage.c - the garage's showroom and shop (see garage.h). */
#include "garage.h"
#include "ui.h"
#include "dgk/app.h"
#include "dgk/gfx.h"
#include "dgk/log.h"
#include "sound.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>

#define FW_PI 3.14159265f

/* The item after (or before) i in its kind, wrapping round. */
static int step_item(int i, int dir)
{
    int k = items[i].kind, j = i;
    do
        j = (j + dir + nitems) % nitems;
    while (items[j].kind != k);
    return j;
}

/* The tractor as it would look with the item shown tried on. */
static void try_on(const garage *g, const career *c, lorry_meshes *m)
{
    const item *shown = &items[g->shown[g->kind < KINDS ? g->kind : KIND_PAINT]];
    uint32_t paint = career_item(c, KIND_PAINT)->value;
    int decal = (int)career_item(c, KIND_DECAL)->value, cab = (int)career_item(c, KIND_CAB)->value;
    if (shown->kind == KIND_PAINT)
        paint = shown->value;
    else if (shown->kind == KIND_DECAL)
        decal = (int)shown->value;
    else if (shown->kind == KIND_CAB)
        cab = (int)shown->value;
    lorry_style(m, paint, decal, cab);
}

static void log_shown(const garage *g, const career *c)
{
    const item *it;
    if (g->kind == GARAGE_REPAIR) {
        dgk_log("FW-MENU show REPAIRS: damage %.1f, $%d", c->damage10 / 10.0f, career_repair_cost(c));
        return;
    }
    it = &items[g->shown[g->kind]];
    dgk_log("FW-MENU show %s %s (%s, $%d, %s)", kind_name[g->kind], it->name, rarity_name[it->rarity], it->price,
            career_fitted(c, g->shown[g->kind]) ? "fitted" : career_owns(c, g->shown[g->kind]) ? "owned" : "for sale");
}

static void flash(garage *g, const char *text, uint32_t rgba)
{
    g->message = text;
    g->message_rgba = rgba;
    g->message_tick = dgk_app.ticks;
}

void garage_open(garage *g, const career *c, lorry_meshes *m)
{
    int k;
    g->open = 1;
    g->kind = KIND_PAINT;
    for (k = 0; k < KINDS; k++)
        g->shown[k] = (int)c->fitted[k];
    g->spin = g->spin_prev = 0.6f;
    g->message = NULL;
    dgk_log("FW-MENU garage open: money %lu", (unsigned long)c->money);
    try_on(g, c, m);
    log_shown(g, c);
}

int garage_tick(garage *g, career *c, lorry_meshes *m)
{
    int changed = 0, i = g->shown[g->kind < KINDS ? g->kind : KIND_PAINT];
    g->spin_prev = g->spin;
    g->spin += 0.35f / DGK_TICK_HZ;                  /* a slow turn of the platform */
    if (dgk_app.key_pressed[DGK_KEY_ESCAPE]) {
        g->open = 0;
        dgk_log("FW-MENU garage close: money %lu", (unsigned long)c->money);
        return 0;
    }
    if (g->kind == GARAGE_REPAIR && dgk_app.key_pressed[DGK_KEY_RETURN]) {
        int paid;
        if (c->damage10 == 0)
            flash(g, "NO DAMAGE", 0xFFFFFFFFu);
        else if ((paid = career_repair(c)) > 0) {
            flash(g, c->damage10 ? "PATCHED UP" : "GOOD AS NEW!", 0x9FFFB0FFu);
            dgk_log("FW-MENU repair for $%d: damage %.1f, money %lu", paid, c->damage10 / 10.0f,
                    (unsigned long)c->money);
            changed = 1;
        } else {
            flash(g, "NOT ENOUGH MONEY", 0xFF5030FFu);
            dgk_log("FW-MENU short of money to repair ($%d, have $%lu)", career_repair_cost(c),
                    (unsigned long)c->money);
        }
    } else if (g->kind == GARAGE_REPAIR && (dgk_app.key_pressed[DGK_KEY_LEFT] || dgk_app.key_pressed[DGK_KEY_RIGHT])) {
        /* (one card) */
    } else if (dgk_app.key_pressed[DGK_KEY_LEFT] || dgk_app.key_pressed[DGK_KEY_RIGHT]) {
        g->shown[g->kind] = step_item(i, dgk_app.key_pressed[DGK_KEY_RIGHT] ? 1 : -1);
        if (g->kind == KIND_HORN) {
            sound_set_horn((int)items[g->shown[g->kind]].value);
            sound_horn_preview();
        }
        try_on(g, c, m);
        log_shown(g, c);
    } else if (dgk_app.key_pressed[DGK_KEY_UP] || dgk_app.key_pressed[DGK_KEY_DOWN]) {
        g->kind = (g->kind + (dgk_app.key_pressed[DGK_KEY_DOWN] ? 1 : GARAGE_TABS - 1)) % GARAGE_TABS;
        try_on(g, c, m);
        log_shown(g, c);
    } else if (dgk_app.key_pressed[DGK_KEY_RETURN]) {
        const item *it = &items[i];
        if (career_fitted(c, i) && it->kind != KIND_LICENCE)
            flash(g, "ALREADY FITTED", 0xFFFFFFFFu);
        else if (career_owns(c, i)) {
            career_fit(c, i);
            flash(g, it->kind == KIND_LICENCE ? "ALREADY HELD" : "FITTED!", 0x9FFFB0FFu);
            dgk_log("FW-MENU fit %s", it->name);
            changed = it->kind != KIND_LICENCE;
        } else if (career_buy(c, i) == 0) {
            flash(g, "BOUGHT!", rarity_rgba[it->rarity]);
            dgk_log("FW-MENU buy %s for $%d: money %lu", it->name, it->price, (unsigned long)c->money);
            changed = 1;
        } else {
            flash(g, "NOT ENOUGH MONEY", 0xFF5030FFu);
            dgk_log("FW-MENU short of money for %s ($%d, have $%lu)", it->name, it->price, (unsigned long)c->money);
        }
    }
    return changed;
}

/* ---- drawing --------------------------------------------------------------- */

static void colour(uint32_t rgba)
{
    glColor4ub((GLubyte)(rgba >> 24), (GLubyte)(rgba >> 16), (GLubyte)(rgba >> 8), (GLubyte)rgba);
}

static void rect(float x, float y, float w, float h, uint32_t top, uint32_t bottom)
{
    glBegin(GL_QUADS);
    dgk_gfx_draws++;
    colour(top);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    colour(bottom);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void centred(const dgk_font *f, float y, float scale, uint32_t rgba, const char *s)
{
    dgk_text(f, 320 - dgk_text_width(f, scale, s) / 2, y, scale, rgba, s);
}

/* The showroom: a sky, a platform with a coloured rim, and the tractor
 * turning on it (with a trailer of the given type behind, or -1). */
void showroom_draw(lorry_meshes *m, float spin, uint32_t rim_rgba, int trailer)
{
    int i;
    rig r;
    dgk_v3 eye = { 13.0f, 6.5f, 9.0f }, at = { 0.5f, 1.6f, 0 }, up = { 0, 1, 0 };
    /* The sky: a gradient behind everything. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    dgk_gfx_overlay_begin();
    rect(0, 0, 640, 300, 0x5FA8E8FFu, 0xFFE0B0FFu);
    rect(0, 300, 640, 180, 0x9A8A7AFFu, 0x5A5048FFu);
    dgk_gfx_overlay_end();
    /* The platform and the tractor on it (a licence: its trailer behind). */
    dgk_gfx_perspective(40.0f, (float)dgk_app.width / dgk_app.height, 2.0f, 80.0f);
    dgk_gfx_look_at(eye, at, up);
    glDisable(GL_CULL_FACE);
    glBegin(GL_TRIANGLE_FAN);
    dgk_gfx_draws++;
    colour(0x6A6E78FFu);
    glVertex3f(0, 0.01f, 0);
    for (i = 0; i <= 32; i++)
        glVertex3f(9.0f * cosf(i * FW_PI / 16), 0.01f, 9.0f * sinf(i * FW_PI / 16));
    glEnd();
    glBegin(GL_QUAD_STRIP);
    dgk_gfx_draws++;                          /* the platform's bright rim */
    colour(rim_rgba);
    for (i = 0; i <= 32; i++) {
        glVertex3f(9.0f * cosf(i * FW_PI / 16), 0.02f, 9.0f * sinf(i * FW_PI / 16));
        glVertex3f(9.4f * cosf(i * FW_PI / 16), 0.02f, 9.4f * sinf(i * FW_PI / 16));
    }
    glEnd();
    glEnable(GL_CULL_FACE);
    rig_init(&r, 0, 0, spin);
    r.x = -1.9f * cosf(spin);                        /* turn about the cab's middle */
    r.y = -1.9f * sinf(spin);
    r.has_trailer = trailer >= 0;
    r.trailer_type = trailer >= 0 ? trailer : 0;
    lorry_shadows_begin();
    lorry_shadow_rig(&r, NULL);
    lorry_shadows_end();
    lorry_draw(m, &r, NULL, NULL);
}

void garage_draw(const garage *g, const career *c, lorry_meshes *m, const dgk_font *font, float alpha)
{
    static const char *const tab_name[GARAGE_TABS] = { "PAINT", "HORNS", "DECALS", "LICENCES", "CABS", "REPAIRS" };
    int repair = g->kind == GARAGE_REPAIR;
    const item *it = &items[g->shown[repair ? KIND_PAINT : g->kind]];
    float spin = g->spin_prev + (g->spin - g->spin_prev) * alpha;
    char line[64];
    int k;
    showroom_draw(m, spin, repair ? 0xC8C8D0FFu : rarity_rgba[it->rarity],
                  !repair && it->kind == KIND_LICENCE ? (int)it->value : -1);
    /* The shop. */
    dgk_gfx_overlay_begin();
    /* The header: the name, the money by its coin, the categories (the shown one on a gold pill). */
    ui_panel(8, 6, 624, 68, 12.0f, UI_PANEL, UI_RIM);
    dgk_text(font, 22, 12, 1.5f, 0xFFD23FFFu, "GARAGE");
    snprintf(line, sizeof line, "%lu", (unsigned long)c->money);
    ui_coin(610 - dgk_text_width(font, 1.5f, line) - 14, 22, 10);
    dgk_text(font, 614 - dgk_text_width(font, 1.5f, line), 12, 1.5f, 0xFFD23FFFu, line);
    ui_panel(22 + g->kind * 100, 42, dgk_text_width(font, 1.0f, tab_name[g->kind]) + 16, 24, 12.0f, 0x3D63B0F0u,
             UI_GOLD);
    for (k = 0; k < GARAGE_TABS; k++)
        dgk_text(font, 30 + k * 100, 48, 1.0f, k == g->kind ? UI_GOLD : 0xC8D0E0FFu, tab_name[k]);
    if (repair) {
        /* The repair card: the damage as a bar, what mending it costs. */
        int cost = career_repair_cost(c);
        ui_panel(110, 352, 420, 116, 14.0f, UI_PANEL, 0xC8C8D0FFu);
        centred(font, 368, 2.0f, 0xFFFFFFFFu, "REPAIRS");
        snprintf(line, sizeof line, "DAMAGE %d%%", (int)((c->damage10 + 9) / 10));
        centred(font, 400, 1.0f, c->damage10 > 500 ? 0xFF6040FFu : 0xFFFFFFFFu, line);
        ui_bar(220, 416, 200, 10, c->damage10 / 1000.0f, c->damage10 > 500 ? 0xFF5030FFu : 0xFFB030FFu);
        if (c->damage10 == 0)
            snprintf(line, sizeof line, "NOTHING TO MEND");
        else if (c->money >= (uint32_t)cost)
            snprintf(line, sizeof line, "$%d  ENTER: REPAIR", cost);
        else
            snprintf(line, sizeof line, "$%d  ENTER: PATCH UP ($%lu)", cost, (unsigned long)c->money);
        centred(font, 434, 1.0f, c->money > 0 || c->damage10 == 0 ? 0xFFFFFFFFu : 0xFF8070FFu, line);
        centred(font, 454, 1.0f, 0x9FFFB0FFu, "UP/DOWN: KIND  ESC: BACK");
        if (g->message && dgk_app.ticks - g->message_tick < 90) {
            float t = (dgk_app.ticks - g->message_tick) / 90.0f, s = 2.4f + 0.5f * sinf(t * 20.0f) * (1 - t);
            centred(font, 200, s, g->message_rgba, g->message);
        }
        dgk_gfx_overlay_end();
        return;
    }
    /* The item's card, rimmed in its rarity's colour: its name, its state. */
    ui_panel(110, 352, 420, 116, 14.0f, UI_PANEL, rarity_rgba[it->rarity]);
    centred(font, 372, 2.0f, rarity_rgba[it->rarity], it->name);
    centred(font, 408, 1.0f, rarity_rgba[it->rarity], rarity_name[it->rarity]);
    if (career_fitted(c, g->shown[g->kind]) && it->kind != KIND_LICENCE)
        snprintf(line, sizeof line, "FITTED");
    else if (career_owns(c, g->shown[g->kind]))
        snprintf(line, sizeof line, it->kind == KIND_LICENCE ? "HELD" : "OWNED  ENTER: FIT");
    else
        snprintf(line, sizeof line, "$%d  ENTER: BUY", it->price);
    centred(font, 432, 1.0f, c->money >= (uint32_t)it->price || career_owns(c, g->shown[g->kind]) ? 0xFFFFFFFFu
                                                                                               : 0xFF8070FFu, line);
    dgk_text(font, 122, 400, 2.0f, 0xFFFFFFFFu, "<");
    dgk_text(font, 498, 400, 2.0f, 0xFFFFFFFFu, ">");
    centred(font, 454, 1.0f, 0x9FFFB0FFu, "LEFT/RIGHT: BROWSE  UP/DOWN: KIND  ESC: BACK");
    if (g->message && dgk_app.ticks - g->message_tick < 90) {
        float t = (dgk_app.ticks - g->message_tick) / 90.0f, s = 2.4f + 0.5f * sinf(t * 20.0f) * (1 - t);
        centred(font, 200, s, g->message_rgba, g->message);
    }
    dgk_gfx_overlay_end();
}
