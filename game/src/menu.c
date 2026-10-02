/* menu.c - the title, pause and options screens (see menu.h). */
#include "game.h"
#include "dgk/gfx.h"
#include "dgk/log.h"
#include "dgk/mix.h"
#include "sound.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const title_items[] = { "DRIVE", "GARAGE", "OPTIONS", "JOYSTICK SET-UP", "QUIT" };
static const char *const pause_items[] = { "RESUME", "OPTIONS", "JOYSTICK SET-UP", "QUIT" };
enum { OPT_DETAIL, OPT_MODE, OPT_VSYNC, OPT_VOLUME, OPT_GOVERNOR, OPT_BACK, OPTS };
static const char *const option_names[OPTS] = { "DETAIL", "SCREEN", "VSYNC", "VOLUME", "GOVERNOR", "BACK" };

static int items_on(int screen)
{
    return screen == SCREEN_TITLE ? 5 : screen == SCREEN_PAUSE ? 4 : OPTS;
}

static void play(game *g)
{
    g->men.screen = SCREEN_PLAY;
    if (!g->men.played) {
        g->men.played = 1;
        dgk_log("FW-PLAY ready");                    /* scripted keys start from here */
    }
}

void menu_start(game *g, int title)
{
    int i;
    char want[16];
    const char *m = dgk_cfg_get(&g->cfg, "video.mode");
    g->men.nmodes = dgk_app_modes(g->men.modes, MENU_MODES);
    snprintf(want, sizeof want, "%dx%d", dgk_app.width, dgk_app.height);
    for (i = 0; i < g->men.nmodes; i++) {           /* the size in the file, else the one in use */
        char s[16];
        snprintf(s, sizeof s, "%dx%d", g->men.modes[i][0], g->men.modes[i][1]);
        if (!strcmp(s, m ? m : want))
            g->men.mode = i;
    }
    dgk_mix_master(DGK_CLAMP(dgk_cfg_int(&g->cfg, "sound.volume", 8), 0, 10) * 32);
    if (title)
        menu_open(g, SCREEN_TITLE);
    else
        play(g);
}

void menu_open(game *g, int screen)
{
    if (screen == SCREEN_OPTIONS)
        g->men.back = g->men.screen;
    g->men.screen = screen;
    g->men.cursor = 0;
    dgk_log("FW-MENU %s", screen == SCREEN_TITLE ? "title" : screen == SCREEN_PAUSE ? "pause" : "options");
}

static void quit_game(void)
{
    dgk_log("FW-MENU quit");
    dgk_app.quit = 1;
}

/* Left or right on an option: its next value, applied now (the screen size
 * from the next start). */
static void change(game *g, int dir)
{
    char v[16];
    int vol;
    switch (g->men.cursor) {
    case OPT_DETAIL:
        g->detail_chosen = (g->detail_chosen + dir + DETAILS) % DETAILS;
        dgk_cfg_set(&g->cfg, "detail", detail_name[g->detail_chosen]);
        g->gov.notch = 0;
        g->det = detail_effective(g->presets, g->detail_chosen, 0);
        fx_caps(&g->fxs, g->det.coins, g->det.confetti, g->det.dust);
        g->cam.zoom = DGK_MIN(g->cam.zoom, g->det.zoom_max);
        break;
    case OPT_MODE:
        if (!g->men.nmodes)
            return;
        g->men.mode = (g->men.mode + dir + g->men.nmodes) % g->men.nmodes;
        snprintf(v, sizeof v, "%dx%d", g->men.modes[g->men.mode][0], g->men.modes[g->men.mode][1]);
        dgk_cfg_set(&g->cfg, "video.mode", v);
        break;
    case OPT_VSYNC:
        dgk_cfg_set_int(&g->cfg, "video.vsync", !dgk_cfg_int(&g->cfg, "video.vsync", 1));
        dgk_app_vsync(dgk_cfg_int(&g->cfg, "video.vsync", 1));
        break;
    case OPT_VOLUME:
        vol = DGK_CLAMP(dgk_cfg_int(&g->cfg, "sound.volume", 8) + dir, 0, 10);
        dgk_cfg_set_int(&g->cfg, "sound.volume", vol);
        dgk_mix_master(vol * 32);
        sound_clunk();                               /* hear the new level */
        break;
    case OPT_GOVERNOR:
        dgk_cfg_set_int(&g->cfg, "governor", !dgk_cfg_int(&g->cfg, "governor", 1));
        g->gov.on = !dgk_app.fixed && !dgk_test_active() && dgk_cfg_int(&g->cfg, "governor", 1);
        g->gov.notch = 0;
        break;
    default:
        return;
    }
    g->men.dirty = 1;
    dgk_log("FW-MENU %s %s", option_names[g->men.cursor],
            g->men.cursor == OPT_DETAIL ? detail_name[g->detail_chosen]
            : g->men.cursor == OPT_MODE ? dgk_cfg_get(&g->cfg, "video.mode")
            : g->men.cursor == OPT_VOLUME ? dgk_cfg_get(&g->cfg, "sound.volume")
            : g->men.cursor == OPT_VSYNC ? dgk_cfg_get(&g->cfg, "video.vsync")
                                         : dgk_cfg_get(&g->cfg, "governor"));
}

static void leave_options(game *g)
{
    if (g->men.dirty) {
        int ok = dgk_cfg_save(&g->cfg, g->cfg_path) == 0;
        dgk_log("FW-CFG saved %s %s: detail %s", g->cfg_path, ok ? "ok" : "FAILED", detail_name[g->detail_chosen]);
        g->men.dirty = 0;
    }
    g->men.screen = g->men.back;
    g->men.cursor = 0;
}

int menu_tick(game *g)
{
    menu *m = &g->men;
    int n = items_on(m->screen), enter = dgk_app.key_pressed[DGK_KEY_RETURN], esc = dgk_app.key_pressed[DGK_KEY_ESCAPE];
    m->spin_prev = m->spin;
    m->spin += 0.35f / DGK_TICK_HZ;
    if (m->screen == SCREEN_PLAY)
        return 0;
    if (dgk_app.key_pressed[DGK_KEY_UP])
        m->cursor = (m->cursor + n - 1) % n;
    if (dgk_app.key_pressed[DGK_KEY_DOWN])
        m->cursor = (m->cursor + 1) % n;
    switch (m->screen) {
    case SCREEN_TITLE:
        if (esc)
            quit_game();
        else if (enter) {
            dgk_log("FW-MENU pick %s", title_items[m->cursor]);
            switch (m->cursor) {
            case 0: play(g); break;
            case 1:
                g->car.money = (uint32_t)DGK_MAX(g->jobs.money, 0);
                garage_open(&g->gar, &g->car, &g->lorry);
                break;
            case 2: menu_open(g, SCREEN_OPTIONS); break;
            case 3: calib_start(&g->cal); break;
            default: quit_game(); break;
            }
        }
        break;
    case SCREEN_PAUSE:
        if (esc)
            play(g);
        else if (enter) {
            dgk_log("FW-MENU pick %s", pause_items[m->cursor]);
            switch (m->cursor) {
            case 0: play(g); break;
            case 1: menu_open(g, SCREEN_OPTIONS); break;
            case 2: calib_start(&g->cal); break;
            default: quit_game(); break;
            }
        }
        break;
    case SCREEN_OPTIONS:
        if (esc || (enter && m->cursor == OPT_BACK))
            leave_options(g);
        else if (dgk_app.key_pressed[DGK_KEY_LEFT] || dgk_app.key_pressed[DGK_KEY_RIGHT])
            change(g, dgk_app.key_pressed[DGK_KEY_RIGHT] ? 1 : -1);
        break;
    default:
        break;
    }
    return m->screen != SCREEN_PLAY;
}

/* ---- drawing --------------------------------------------------------------- */

static void panel(float x, float y, float w, float h, uint32_t rgba)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub((GLubyte)(rgba >> 24), (GLubyte)(rgba >> 16), (GLubyte)(rgba >> 8), (GLubyte)rgba);
    glBegin(GL_QUADS);
    dgk_gfx_draws++;
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
    glDisable(GL_BLEND);
}

static void centred(game *g, float y, float scale, uint32_t rgba, const char *s)
{
    dgk_text(&g->font, 320 - dgk_text_width(&g->font, scale, s) / 2, y, scale, rgba, s);
}

/* A list of items, the chosen one lit and pointed at. */
static void list(game *g, const char *const *items, int n, float y)
{
    int i;
    for (i = 0; i < n; i++) {
        uint32_t rgba = i == g->men.cursor ? 0xFFD23FFFu : 0xFFFFFFFFu;
        centred(g, y + i * 30, 1.5f, rgba, items[i]);
        if (i == g->men.cursor) {
            float w = dgk_text_width(&g->font, 1.5f, items[i]);
            dgk_text(&g->font, 320 - w / 2 - 32, y + i * 30, 1.5f, rgba, ">");
        }
    }
}

static void options(game *g)
{
    char line[48];
    int i;
    panel(90, 100, 460, 280, 0x000000C0u);
    centred(g, 112, 2.0f, 0xFFD23FFFu, "OPTIONS");
    for (i = 0; i < OPTS; i++) {
        uint32_t rgba = i == g->men.cursor ? 0xFFD23FFFu : 0xFFFFFFFFu;
        const char *v = "";
        switch (i) {
        case OPT_DETAIL:
            snprintf(line, sizeof line, "< %s >", detail_name[g->detail_chosen]);
            v = line;
            break;
        case OPT_MODE:
            if (g->men.nmodes)
                snprintf(line, sizeof line, "< %dX%d >", g->men.modes[g->men.mode][0], g->men.modes[g->men.mode][1]);
            else
                snprintf(line, sizeof line, "%dX%d", dgk_app.width, dgk_app.height);
            v = line;
            break;
        case OPT_VSYNC:
            v = dgk_cfg_int(&g->cfg, "video.vsync", 1) ? "< ON >" : "< OFF >";
            break;
        case OPT_VOLUME:
            snprintf(line, sizeof line, "< %d >", DGK_CLAMP(dgk_cfg_int(&g->cfg, "sound.volume", 8), 0, 10));
            v = line;
            break;
        case OPT_GOVERNOR:
            v = dgk_cfg_int(&g->cfg, "governor", 1) ? "< ON >" : "< OFF >";
            break;
        default:
            break;
        }
        dgk_text(&g->font, 110, 160 + i * 30, 1.5f, rgba, option_names[i]);
        {
            char up[48];
            int k;
            for (k = 0; v[k] && k < (int)sizeof up - 1; k++)  /* the font has capitals */
                up[k] = (char)(v[k] >= 'a' && v[k] <= 'z' ? v[k] - 32 : v[k]);
            up[k] = 0;
            dgk_text(&g->font, 530 - dgk_text_width(&g->font, 1.5f, up), 160 + i * 30, 1.5f, rgba, up);
        }
    }
    if (g->men.nmodes && (g->men.modes[g->men.mode][0] != dgk_app.width || g->men.modes[g->men.mode][1] != dgk_app.height))
        centred(g, 344, 1.0f, 0x9FFFB0FFu, "THE NEW SCREEN SIZE FROM THE NEXT START");
    else
        centred(g, 344, 1.0f, 0x9FFFB0FFu, "LEFT/RIGHT: CHANGE  ESC: BACK");
}

int menu_draw_scene(game *g, float alpha)
{
    menu *m = &g->men;
    float spin = m->spin_prev + (m->spin - m->spin_prev) * alpha;
    if (m->screen != SCREEN_TITLE && !(m->screen == SCREEN_OPTIONS && m->back == SCREEN_TITLE))
        return 0;
    showroom_draw(&g->lorry, spin, 0xFFD23FFFu, -1);
    dgk_gfx_overlay_begin();
    if (m->screen == SCREEN_TITLE) {
        /* The name, bouncing a little, its shadow first. */
        float b = fabsf(sinf((float)dgk_app.ticks * 0.05f)) * 6.0f;
        char line[64];
        centred(g, 34 - b, 4.0f, 0xFFD23FFFu, "FIFTH WHEEL");
        centred(g, 100, 1.0f, 0xFFFFFFFFu, "DELIVERIES FOR A TOY LORRY");
        panel(190, 286, 260, 168, 0x000000A0u);
        list(g, title_items, 5, 296);
        snprintf(line, sizeof line, "$%lu  %lu DELIVERED", (unsigned long)DGK_MAX(g->jobs.money, 0),
                 (unsigned long)g->car.delivered);
        dgk_text(&g->font, 14, 456, 1.0f, 0xFFD23FFFu, line);
    } else
        options(g);
    dgk_gfx_overlay_end();
    return 1;
}

void menu_draw_overlay(game *g)
{
    if (g->men.screen == SCREEN_PAUSE) {
        dgk_gfx_overlay_begin();
        panel(0, 0, 640, 480, 0x00000070u);
        panel(190, 150, 260, 180, 0x000000C0u);
        centred(g, 162, 2.0f, 0xFFD23FFFu, "PAUSED");
        list(g, pause_items, 4, 210);
        dgk_gfx_overlay_end();
    } else if (g->men.screen == SCREEN_OPTIONS) {
        dgk_gfx_overlay_begin();
        panel(0, 0, 640, 480, 0x00000070u);
        options(g);
        dgk_gfx_overlay_end();
    }
}
