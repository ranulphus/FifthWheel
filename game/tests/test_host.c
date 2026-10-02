/* test_host.c - unit tests that need no screen (make tests-host): the
 * governor and the detail presets, settings and saves (corrupt and
 * half-written ones too), the career's shop. */
#include "career.h"
#include "detail.h"
#include "dgk/cfg.h"
#include "dgk/save.h"
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

int main(void)
{
    governor_steps();
    presets();
    settings();
    saves();
    shop();
    printf("tests-host: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
