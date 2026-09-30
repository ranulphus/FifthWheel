/* bench.c - DOSBench records (see dgk/bench.h). */
#include "dgk/bench.h"
#include "dgk/app.h"
#include "dgk/log.h"
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DGK_DOS
#include <GL/dosgl.h>
#endif

static char run_id[12], prog_name[16], mode[16];
static char test_name[24];
static int warmup, nframes;
static uint32_t frame_us[DGK_BENCH_MAX_FRAMES], tris_total;
static uint64_t total_us;

static void emit(const char *line)
{
#ifdef DGK_DOS
    const char *path = "C:\\OUT\\RESULTS.TXT";
#else
    const char *path = "out/RESULTS.TXT";
#endif
    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
    dgk_log("HX-STAT bench %s", line);
}

/* A value for a key=value field: no spaces. */
static void field(char *out, size_t n, const char *s)
{
    size_t i;
    for (i = 0; i + 1 < n && s[i]; i++)
        out[i] = s[i] == ' ' ? '_' : s[i];
    out[i] = 0;
}

void dgk_bench_run(const char *prog, const char *ver)
{
    char line[400], impl[64], card[64];
    uint32_t seed = dgk_crc32(0, &total_us, sizeof total_us) ^ (uint32_t)dgk_now_us();
    snprintf(run_id, sizeof run_id, "%08lx", (unsigned long)dgk_crc32(seed, prog, strlen(prog)));
    snprintf(prog_name, sizeof prog_name, "%s", prog);
    snprintf(mode, sizeof mode, "%dx%d", dgk_app.width, dgk_app.height);
#ifdef DGK_DOS
    field(impl, sizeof impl, dglVersion());
    {
        const DGLDeviceInfo *d = dglGetDeviceInfo();
        char c[64];
        snprintf(c, sizeof c, "%s_rev%u_%luMB", d ? d->chip_name : "?", d ? d->revision : 0,
                 d ? d->vram_bytes >> 20 : 0UL);
        field(card, sizeof card, c);
    }
#else
    field(impl, sizeof impl, (const char *)glGetString(GL_VERSION));
    field(card, sizeof card, (const char *)glGetString(GL_RENDERER));
#endif
    snprintf(line, sizeof line,
             "H run=%s prog=%s ver=%s build=%s tag=L api=opengl impl=%s card=%s mode=%s vsync=0 submit=arrays "
             "timer=%s cpu_mhz=0 tex_kb=0 quick=0",
             run_id, prog_name, ver, ver, impl, card, mode,
#ifdef DGK_DOS
             "uclock"
#else
             "clock_gettime"
#endif
    );
    emit(line);
}

void dgk_bench_begin(const char *test, int warmup_frames)
{
    snprintf(test_name, sizeof test_name, "%s", test);
    warmup = warmup_frames;
    nframes = 0;
    tris_total = 0;
    total_us = 0;
}

void dgk_bench_frame(uint64_t us, uint32_t tris)
{
    if (warmup > 0) {
        warmup--;
        return;
    }
    if (nframes < DGK_BENCH_MAX_FRAMES)
        frame_us[nframes++] = (uint32_t)us;
    total_us += us;
    tris_total += tris;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void dgk_bench_end(const char *status, const char *notes)
{
    char line[480];
    double secs = total_us / 1e6, avg, med = 0, p99 = 0, mn = 0, mx = 0;
    if (nframes) {
        qsort(frame_us, (size_t)nframes, sizeof frame_us[0], cmp_u32);
        med = frame_us[nframes / 2] / 1e3;
        p99 = frame_us[(nframes * 99) / 100 < nframes ? (nframes * 99) / 100 : nframes - 1] / 1e3;
        mn = frame_us[0] / 1e3;
        mx = frame_us[nframes - 1] / 1e3;
    }
    avg = nframes ? secs * 1e3 / nframes : 0;
    snprintf(line, sizeof line,
             "T run=%s prog=%s tag=L api=opengl mode=%s test=%s group=game status=%s frames=%d secs=%.3f fps=%.2f "
             "avg_ms=%.3f med_ms=%.3f p99_ms=%.3f min_ms=%.3f max_ms=%.3f submit_ms=0.000 tris_frame=%lu "
             "ktris_s=%.1f crc=0 %s",
             run_id, prog_name, mode, test_name, status, nframes, secs, secs > 0 ? nframes / secs : 0, avg, med,
             p99, mn, mx, nframes ? (unsigned long)(tris_total / (uint32_t)nframes) : 0UL,
             secs > 0 ? tris_total / secs / 1e3 : 0, notes ? notes : "");
    emit(line);
}
