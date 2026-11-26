/* test_host.c - unit tests that need no screen (make tests-host): the
 * governor and the detail presets, settings and saves (corrupt and
 * half-written ones too), the career's shop and repairs, the ways along a
 * town's streets, the music's tunes. */
#include "career.h"
#include "detail.h"
#include "music.h"
#include "townroute.h"
#include "dgk/app.h"
#include "dgk/cfg.h"
#include "dgk/midi.h"
#include "dgk/save.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;

#define CHECK(cond, ...)                                                                                  \
    do {                                                                                                  \
        checks++;                                                                                         \
        if (!(cond)) {                                                                                    \
            failures++;                                                                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                   \
            printf(__VA_ARGS__);                                                                          \
            printf("\n");                                                                                 \
        }                                                                                                 \
    } while (0)

static void governor_steps(void)
{
    governor g;
    int i, changed = 0;
    memset(&g, 0, sizeof g);
    g.on = 1;
    for (i = 0; i < 40; i++)                         /* 1.6 s at 25 fps: not yet */
        changed |= governor_frame(&g, 40.0f);
    CHECK(!changed && g.notch == 0, "stepped after 1.6 s slow: notch %d", g.notch);
    for (i = 0; i < 20; i++)
        changed |= governor_frame(&g, 40.0f);
    CHECK(changed && g.notch == 1, "after 2.4 s slow: notch %d", g.notch);
    for (i = 0; i < 400; i++)                        /* 8 s at 50 fps: back up */
        governor_frame(&g, 20.0f);
    CHECK(g.notch == 0, "after 8 s fast: notch %d", g.notch);
    g.on = 0;
    for (i = 0; i < 400; i++)
        governor_frame(&g, 80.0f);
    CHECK(g.notch == 0, "off, yet stepped: notch %d", g.notch);
}

static void presets(void)
{
    detail p[DETAILS], d;
    FILE *f;
    detail_load(p, NULL);
    CHECK(p[DETAIL_LOW].far < p[DETAIL_MEDIUM].far && p[DETAIL_MEDIUM].far < p[DETAIL_HIGH].far, "far planes in order");
    d = detail_effective(p, DETAIL_MEDIUM, 1);
    CHECK(d.far == p[DETAIL_LOW].far && d.confetti == p[DETAIL_LOW].confetti, "a notch below medium is low");
    d = detail_effective(p, DETAIL_LOW, 2);
    CHECK(d.far < p[DETAIL_LOW].far * 0.7f, "two notches below low: far %.1f", d.far);
    f = fopen("out/test-budget.cfg", "w");
    fprintf(f, "# test\nlow.far = 95\nhigh.confetti = 999\nmedium.zoom_max = 1\n");
    fclose(f);
    detail_load(p, "out/test-budget.cfg");
    CHECK(p[DETAIL_LOW].far == 95.0f, "budget's low.far: %.1f", p[DETAIL_LOW].far);
    CHECK(p[DETAIL_HIGH].confetti == 64, "confetti held to the pool: %d", p[DETAIL_HIGH].confetti);
    CHECK(p[DETAIL_MEDIUM].zoom_max == 1, "medium.zoom_max: %d", p[DETAIL_MEDIUM].zoom_max);
    CHECK(detail_parse("high") == DETAIL_HIGH && detail_parse("ultra") == -1, "parse");
    /* Each card's own preset (the owner's targets). */
    CHECK(detail_for_renderer("DOS-GL on MGA-G200 (AGP)") == DETAIL_LOW, "G200: low");
    CHECK(detail_for_renderer("DOS-GL on MGA-G100 (PCI)") == DETAIL_LOW, "G100: low");
    CHECK(detail_for_renderer("DOS-GL on Software") == DETAIL_LOW, "software: low");
    CHECK(detail_for_renderer("DOS-GL on MGA-G400/G450") == DETAIL_MEDIUM, "G400: medium");
    CHECK(detail_for_renderer("DOS-GL on MGA-G450") == DETAIL_MEDIUM, "G450: medium");
    CHECK(detail_for_renderer("DOS-GL on Riva TNT2 M64") == DETAIL_MEDIUM, "TNT2 M64: medium");
    CHECK(detail_for_renderer("DOS-GL on Vanta") == DETAIL_LOW, "a Vanta (a cut-down TNT2): low");
    CHECK(detail_for_renderer("DOS-GL on ATI Radeon 7500") == DETAIL_HIGH, "Radeon 7500: high");
    CHECK(detail_for_renderer("Mesa Intel(R) UHD Graphics") == DETAIL_HIGH, "a desktop's GL: high");
    CHECK(detail_for_renderer(NULL) == DETAIL_LOW, "no renderer: low");
}

static void settings(void)
{
    dgk_cfg c, back;
    memset(&c, 0, sizeof c);
    dgk_cfg_set(&c, "detail", "low");
    dgk_cfg_set_int(&c, "sound.volume", 7);
    dgk_cfg_set(&c, "detail", "high");               /* replaces */
    CHECK(c.n == 2, "two keys: %d", c.n);
    CHECK(dgk_cfg_save(&c, "out/test.cfg") == 0, "saved");
    CHECK(dgk_cfg_load(&back, "out/test.cfg") == 0, "loaded");
    CHECK(!strcmp(dgk_cfg_get(&back, "detail"), "high") && dgk_cfg_int(&back, "sound.volume", 0) == 7,
          "round trip: %s %d", dgk_cfg_get(&back, "detail"), dgk_cfg_int(&back, "sound.volume", 0));
    CHECK(dgk_cfg_int(&back, "missing", 42) == 42, "fallback");
}

static void saves(void)
{
    career a, b;
    char tmp[96];
    FILE *f;
    long size;
    career_new(&a);
    a.money = 1234;
    CHECK(dgk_save_write("out/test.dat", CAREER_MAGIC, CAREER_VERSION, &a, sizeof a) == 0, "written");
    CHECK(dgk_save_read("out/test.dat", CAREER_MAGIC, CAREER_VERSION, &b, sizeof b) == DGK_SAVE_OK &&
          b.money == 1234, "read back: %lu", (unsigned long)b.money);
    CHECK(dgk_save_read("out/test.dat", CAREER_MAGIC, CAREER_VERSION + 1, &b, sizeof b) == DGK_SAVE_BAD,
          "another version refused");
    f = fopen("out/test.dat", "r+b");                /* a flipped bit in the data */
    fseek(f, 20, SEEK_SET);
    fputc(0x5A, f);
    fclose(f);
    CHECK(dgk_save_read("out/test.dat", CAREER_MAGIC, CAREER_VERSION, &b, sizeof b) == DGK_SAVE_BAD,
          "a damaged save refused");
    /* A crash after the old file went and before the new one was renamed:
     * only the .TMP is there, and it is taken up. */
    dgk_save_write("out/test.dat", CAREER_MAGIC, CAREER_VERSION, &a, sizeof a);
    rename("out/test.dat", dgk_temp_path("out/test.dat", tmp, sizeof tmp));
    CHECK(dgk_save_read("out/test.dat", CAREER_MAGIC, CAREER_VERSION, &b, sizeof b) == DGK_SAVE_OK &&
          b.money == 1234, "the .TMP taken up");
    f = fopen("out/test.dat", "rb");
    CHECK(f != NULL, "and renamed into place");
    if (f) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
        CHECK(size == 16 + (long)sizeof a, "size %ld", size);
    }
    CHECK(!strcmp(dgk_temp_path("CAREER.DAT", tmp, sizeof tmp), "CAREER.TMP"), "8.3 temporary: %s", tmp);
}

static void shop(void)
{
    career c;
    int sky = -1, tankers = -1, i;
    career_new(&c);
    for (i = 0; i < nitems; i++) {
        if (!strcmp(items[i].name, "SKY BLUE"))
            sky = i;
        if (!strcmp(items[i].name, "TANKERS"))
            tankers = i;
    }
    CHECK(c.money == 250 && career_licences(&c) == 1u, "a new career: $%lu, licences %lx", (unsigned long)c.money,
          (unsigned long)career_licences(&c));
    CHECK(career_buy(&c, sky) == 0 && c.money == 100 && career_fitted(&c, sky), "bought and fitted");
    CHECK(career_buy(&c, sky) == -2, "not twice");
    CHECK(career_buy(&c, tankers) == -1 && c.money == 100, "not without the money");
    c.money = 5000;
    CHECK(career_buy(&c, tankers) == 0 && (career_licences(&c) >> 2 & 1u), "the tanker licence");
    CHECK(career_fit(&c, 0) == 0 && career_fitted(&c, 0), "back to the first paint");
}

static void repairs(void)
{
    career c;
    career_new(&c);
    CHECK(sizeof(career) == 52, "the career's save is %u bytes (52: older saves load)", (unsigned)sizeof(career));
    CHECK(c.damage10 == 0 && career_repair_cost(&c) == 0 && career_repair(&c) == 0, "nothing to mend");
    c.damage10 = 635;                                /* 63.5 points: $190.5, rounded up */
    c.money = 1000;
    CHECK(career_repair_cost(&c) == 191, "cost %d", career_repair_cost(&c));
    CHECK(career_repair(&c) == 191 && c.damage10 == 0 && c.money == 809, "mended: $%lu left, %lu", (unsigned long)c.money,
          (unsigned long)c.damage10);
    c.damage10 = 1000;
    c.money = 100;
    CHECK(career_repair(&c) == 100 && c.money == 0 && c.damage10 == 667, "patched up: damage %lu",
          (unsigned long)c.damage10);
    CHECK(career_repair(&c) == 0 && c.damage10 == 667, "no money, no mending");
}

/* A way's points: none off the streets (5 m from a street's middle) or the roundabout's road (5 m from its ring,
 * clear of its island), no turn back (more than 100 degrees within 12 m), and it ends at the stop. */
static int way_ok(const wg_town *t, const float (*p)[2], int n, float x1, float y1, char *why)
{
    float c = cosf(t->heading), s = sinf(t->heading);
    int i;
    if (n < 2 || fabsf(p[n - 1][0] - x1) > 0.01f || fabsf(p[n - 1][1] - y1) > 0.01f)
        return sprintf(why, "ends at (%.1f, %.1f), not the stop", n ? p[n - 1][0] : 0, n ? p[n - 1][1] : 0), 0;
    for (i = 0; i < n; i++) {
        float dx = p[i][0] - t->x, dy = p[i][1] - t->y, u = dx * c + dy * s, v = -dx * s + dy * c;
        float du = fabsf(u - WG_GRID * floorf(u / WG_GRID + 0.5f)), dv = fabsf(v - WG_GRID * floorf(v / WG_GRID + 0.5f));
        float r = sqrtf(u * u + v * v);
        if (r < WG_RING_ISLAND + 1.5f)
            return sprintf(why, "point %d on the island (%.1f m from the middle)", i, r), 0;
        if (du > 5.0f && dv > 5.0f && fabsf(r - WG_RING_R) > 5.0f)
            return sprintf(why, "point %d off the streets (%.1f, %.1f)", i, u, v), 0;
    }
    for (i = 1; i + 1 < n; i++) {
        int k = i + 1;
        float ax = p[i][0] - p[i - 1][0], ay = p[i][1] - p[i - 1][1], bx, by, la = sqrtf(ax * ax + ay * ay), lb;
        while (k + 1 < n && (p[k][0] - p[i][0]) * (p[k][0] - p[i][0]) + (p[k][1] - p[i][1]) * (p[k][1] - p[i][1]) < 4.0f)
            k++;
        bx = p[k][0] - p[i][0];
        by = p[k][1] - p[i][1];
        lb = sqrtf(bx * bx + by * by);
        if (la > 0.5f && lb > 0.5f && (ax * bx + ay * by) / (la * lb) < cosf(100.0f * WG_PI / 180.0f))
            return sprintf(why, "turns back at point %d", i), 0;
    }
    return 1;
}

static void town_ways(void)
{
    static float way[2048][2];
    wg_town t;
    char why[96];
    float metres;
    int i, n, back, bad = 0, backs = 0;
    uint32_t h = 12345;
    memset(&t, 0, sizeof t);
    t.x = 1000;
    t.y = 1000;
    t.heading = 0.5f;
    t.r = 170;
    t.blocks = 3;
    CHECK(town_at(&t, 1, 1000 + 150, 1000, 0) == 0 && town_at(&t, 1, 1300, 1300, 0) == -1, "town_at");
    /* 400 ways between random lane points 12 m or more from a crossing, as stops are (and from the roundabout, as
     * a road's end), facing along the street either way (give or take 20 degrees). */
    for (i = 0; i < 400; i++) {
        float p[2][2], hd = 0, street = 0;
        int e;
        for (e = 0; e < 2; e++) {
            float u, v, c = cosf(t.heading), s = sinf(t.heading);
            int k, along;
            h = h * 1103515245u + 12345u;
            k = (int)(h >> 16) % 7 - 3;
            h = h * 1103515245u + 12345u;
            along = (int)(h >> 16) % 370 - 185;
            if (fabsf(along - WG_GRID * floorf(along / WG_GRID + 0.5f)) < 12.0f)
                along += 24;
            if (i % 5 == 0 && e == 0) {               /* from the ring, where a road reaches it */
                u = 24.5f * cosf((float)along);
                v = 24.5f * sinf((float)along);
            } else if ((h >> 8) & 1) {
                u = k * WG_GRID + 2.25f;
                v = (float)along;
                if (e == 0)
                    street = WG_PI / 2;
            } else {
                u = (float)along;
                v = k * WG_GRID - 2.25f;
            }
            if (u * u + v * v < 40.0f * 40.0f && !(i % 5 == 0 && e == 0)) {
                if (fabsf(v) < 3.0f)                  /* (stops are not by the roundabout: along their street) */
                    u += 70.0f;
                else
                    v += 70.0f;
            }
            p[e][0] = t.x + c * u - s * v;
            p[e][1] = t.y + s * u + c * v;
        }
        h = h * 1103515245u + 12345u;
        hd = t.heading + street + ((h >> 20) & 1 ? WG_PI : 0) + ((float)((h >> 8) & 255) / 255.0f - 0.5f) * 0.7f;
        n = town_path(&t, p[0][0], p[0][1], hd, p[1][0], p[1][1], way, 2048, &metres, &back);
        backs += back;
        if (!way_ok(&t, (const float (*)[2])way, n, p[1][0], p[1][1], why) && bad++ < 3)
            printf("town way %d: %s\n", i, why);
    }
    CHECK(bad == 0, "%d of 400 town ways bad", bad);
    CHECK(backs < 40, "%d of 400 start against the rig (only at the town's edge)", backs);
}

/* The music (music.c and its tunes) on a dgk of the test's own: its messages kept (note-ons counted by channel,
 * times checked in order and ahead of the clock). */
dgk_app_state dgk_app;
uint64_t dgk_now_us(void) { return 0; }
static int notes, drum_notes, bad_order, late, opened;
static uint32_t last_at, now_ms_seen;
static int programs[16];

int dgk_midi_open(int want) { opened = want != DGK_MIDI_OFF; return opened ? DGK_MIDI_FM : DGK_MIDI_OFF; }
const char *dgk_midi_name(void) { return "the test's"; }
void dgk_midi_volume(int percent) { DGK_UNUSED(percent); }
void dgk_midi_panic(void) { last_at = 0; }
void dgk_midi_close(void) { opened = 0; }
void dgk_midi_service(uint32_t now) { now_ms_seen = now; }
uint32_t dgk_midi_hash(void) { return 0; }
uint32_t dgk_midi_count(void) { return 0; }
void dgk_midi_send(uint32_t at, uint8_t s, uint8_t d1, uint8_t d2)
{
    if (at < last_at)
        bad_order++;
    if (at + 20 < now_ms_seen)                       /* (sent after its time: more than a tick late) */
        late++;
    last_at = at;
    if ((s & 0xF0) == 0xC0)
        programs[s & 15] = d1 + 1;
    if ((s & 0xF0) == 0x90 && d2 > 0) {
        if ((s & 15) == 9)
            drum_notes++;
        else
            notes++;
    }
}

static void ticks(double s)
{
    int n = (int)(s * DGK_TICK_HZ);
    while (n-- > 0) {
        dgk_app.ticks++;
        music_update();
    }
}

static void music(void)
{
    static const char *names[] = { "title", "garage", "drive1", "drive2", "drive3" };
    size_t i;
    dgk_app.sound = 1;
    dgk_app.fixed = 1;
    music_init();
    CHECK(opened, "music_init opened nothing");
    CHECK(music_ntunes == 5, "%d tunes", music_ntunes);
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        int c, progs = 0;
        notes = drum_notes = bad_order = late = 0;
        memset(programs, 0, sizeof programs);
        CHECK(music_play(names[i]), "no tune %s", names[i]);
        CHECK(music_playing() && !strcmp(music_playing(), names[i]), "%s does not play", names[i]);
        /* Past every tune's end: it plays on. (Over 80 s it sent every note the files hold: title 462, garage
         * 566, drive1 586, drive2 576, drive3 696, give or take the notes at the cut, 2026-11-26.) */
        ticks(80);
        for (c = 0; c < 16; c++)
            progs += programs[c] != 0;
        CHECK(music_playing() && !strcmp(music_playing(), names[i]), "%s stopped", names[i]);
        CHECK(notes > 400 && drum_notes > 300, "%s: %d notes, %d drum notes", names[i], notes, drum_notes);
        CHECK(progs >= 3, "%s: programs set on %d channels", names[i], progs);
        CHECK(bad_order == 0 && late == 0, "%s: %d sent out of order, %d late", names[i], bad_order, late);
    }
    CHECK(!music_play("nothing"), "a tune that is not there plays");
    music_want(MUSIC_DRIVING);                       /* the driving tunes in turn: each twice */
    CHECK(music_playing() && !strcmp(music_playing(), "drive1"), "driving starts with %s", music_playing());
    ticks(2 * 58.2 + 1);
    CHECK(music_playing() && !strcmp(music_playing(), "drive2"), "after drive1 twice: %s", music_playing());
    music_want(MUSIC_GARAGE);
    CHECK(music_playing() && !strcmp(music_playing(), "garage"), "the garage plays %s", music_playing());
    music_want(MUSIC_OFF);
    CHECK(!music_playing(), "off, yet %s plays", music_playing());
    music_shutdown();
    CHECK(!opened, "music_shutdown left dgk's music open");
}

int main(void)
{
    governor_steps();
    presets();
    settings();
    saves();
    shop();
    repairs();
    town_ways();
    music();
    printf("tests-host: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
