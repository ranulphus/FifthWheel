/* fwtune.c - Fifth Wheel's music: tunes written as text (data/music/NAME.fwt)
 * made into standard MIDI files (format 1, 96 ticks a beat), and a C file
 * holding them for the game (build/gen/music_gen.c, in the order given).
 *
 *   fwtune OUT.c OUTDIR TUNE.fwt...
 *
 * A tune:
 *   title Open Road Rag          its name
 *   tempo 132                    beats a minute
 *   meter 4/4                    beats a bar / the beat's note (bars are checked against it)
 *   swing 0.5                    off-beat eighths late, 0 (straight) to 1 (a triplet's third)
 *   define NAME ... enddef       a phrase; $NAME (or $NAME x3) puts it in a part
 *   part NAME ch N prog P vol V  a part on MIDI channel N (1-16; 10: drums), GM program P (0-127)
 *   ...music...                  until the next part
 * Music: notes c4 f#5 bb3 (middle C is c4), r a rest, then /4 /8 /16 /2 /1
 * for the length (dotted /4., a triplet's /8t; without one, the last length);
 * chords c4+e4+g4/2; a trailing _ ties a note to the next of its pitch; ! loud,
 * ? soft; | a bar line. Drums: bd sd hh oh lt mt ht cr rd cl (kick, snare,
 * hi-hat closed and open, toms low to high, crash, ride, a clap). A comment:
 * # at a line's start or after a space. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PPQ 96
#define MAX_EVENTS 65536
#define MAX_TEXT (1 << 20)
#define MAX_DEFS 64
#define MAX_PARTS 16

typedef struct ev {
    long tick;
    unsigned char b[3];
    int n;
} ev;

static ev evs[MAX_EVENTS];
static int nevs;
static const char *file;
static int line_no;

static void die(const char *msg, const char *what)
{
    fprintf(stderr, "fwtune: %s:%d: %s%s%s\n", file, line_no, msg, what ? ": " : "", what ? what : "");
    exit(1);
}

static void add(long tick, int a, int b, int c, int n)
{
    if (nevs == MAX_EVENTS)
        die("too many events", NULL);
    evs[nevs].tick = tick;
    evs[nevs].b[0] = (unsigned char)a;
    evs[nevs].b[1] = (unsigned char)b;
    evs[nevs].b[2] = (unsigned char)c;
    evs[nevs].n = n;
    nevs++;
}

/* A sorted order: by tick, offs before ons at the same tick (a repeated note restarts cleanly). */
static int cmp_ev(const void *a, const void *b)
{
    const ev *x = a, *y = b;
    int ox, oy;
    if (x->tick != y->tick)
        return x->tick < y->tick ? -1 : 1;
    ox = (x->b[0] & 0xF0) == 0x80 || ((x->b[0] & 0xF0) == 0x90 && x->b[2] == 0);
    oy = (y->b[0] & 0xF0) == 0x80 || ((y->b[0] & 0xF0) == 0x90 && y->b[2] == 0);
    if (ox != oy)
        return ox ? -1 : 1;
    return (x < y) ? -1 : 1;
}

/* ---- a part's music ------------------------------------------------------- */

typedef struct partstate {
    int ch, drums;
    long t, bar_start, bar_len;
    int len;                            /* ticks of the last length */
    int tie_note[8], tie_n;             /* notes tied on: their ons were sent */
    long tie_on[8];
    double swing;
} partstate;

static int drum_note(const char *s)
{
    static const struct { const char *n; int k; } d[] = {
        { "bd", 36 }, { "sd", 38 }, { "hh", 42 }, { "oh", 46 }, { "lt", 45 }, { "mt", 47 }, { "ht", 50 },
        { "cr", 49 }, { "rd", 51 }, { "cl", 39 }, { "ph", 44 },
    };
    size_t i;
    for (i = 0; i < sizeof d / sizeof d[0]; i++)
        if (!strcmp(s, d[i].n))
            return d[i].k;
    return -1;
}

static int pitch(const char *s, int drums)
{
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* a b c d e f g */
    int n, oct;
    if (drums)
        return drum_note(s);
    if (*s < 'a' || *s > 'g')
        return -1;
    n = base[*s - 'a'];
    s++;
    while (*s == '#' || *s == 'b') {
        n += *s == '#' ? 1 : -1;
        s++;
    }
    if (!isdigit((unsigned char)*s))
        return -1;
    oct = atoi(s);
    return 12 * (oct + 1) + n;           /* c4 = 60 */
}

/* Swing: an off-beat eighth starts late by swing/6 of a beat (and the note before ends there). */
static long swung(const partstate *p, long t)
{
    long in_beat = t % PPQ;
    if (p->swing > 0 && in_beat == PPQ / 2)
        return t + (long)(p->swing * PPQ / 6.0 + 0.5);
    return t;
}

static void token(partstate *p, char *tok)
{
    char *slash, *q, *names[8];
    int nnames = 0, i, vel = 90, tie = 0;
    long start, end;
    if (!strcmp(tok, "|")) {
        if (p->t == p->bar_start)
            return;                                   /* (a phrase's bar line beside the part's: one) */
        if (p->t - p->bar_start != p->bar_len) {
            char m[64];
            snprintf(m, sizeof m, "%ld ticks in the bar, not %ld", p->t - p->bar_start, p->bar_len);
            die("a bar's length", m);
        }
        p->bar_start = p->t;
        return;
    }
    slash = strchr(tok, '/');
    if (slash) {
        int den = atoi(slash + 1);
        double l;
        if (den != 1 && den != 2 && den != 4 && den != 8 && den != 16 && den != 32)
            die("a length", slash);
        l = PPQ * 4.0 / den;
        for (q = slash + 1; *q; q++) {
            if (*q == '.')
                l *= 1.5;
            else if (*q == 't')
                l = l * 2.0 / 3.0;
        }
        p->len = (int)(l + 0.5);
        *slash = 0;
        for (q = slash + 1; *q; q++) {
            if (*q == '_')
                tie = 1;
            else if (*q == '!')
                vel = 118;
            else if (*q == '?')
                vel = 62;
        }
    }
    while (*tok && (tok[strlen(tok) - 1] == '!' || tok[strlen(tok) - 1] == '?' || tok[strlen(tok) - 1] == '_')) {
        char c = tok[strlen(tok) - 1];
        if (c == '!')
            vel = 118;
        else if (c == '?')
            vel = 62;
        else
            tie = 1;
        tok[strlen(tok) - 1] = 0;
    }
    if (!p->len)
        die("a note before any length", tok);
    start = swung(p, p->t);
    end = swung(p, p->t + p->len);
    p->t += p->len;
    if (!strcmp(tok, "r"))
        return;
    for (q = strtok(tok, "+"); q && nnames < 8; q = strtok(NULL, "+"))
        names[nnames++] = q;
    for (i = 0; i < nnames; i++) {
        int n = pitch(names[i], p->drums), k, was = -1;
        if (n < 0 || n > 127)
            die("a note", names[i]);
        for (k = 0; k < p->tie_n; k++)
            if (p->tie_note[k] == n)
                was = k;
        if (was < 0)
            add(start, 0x90 | p->ch, n, vel, 3);
        else {                                       /* tied on: no new start */
            p->tie_note[was] = p->tie_note[--p->tie_n];
            p->tie_on[was] = p->tie_on[p->tie_n];
        }
        if (tie && !p->drums && p->tie_n < 8) {
            p->tie_note[p->tie_n] = n;
            p->tie_on[p->tie_n++] = start;
        } else
            add(p->drums ? start + PPQ / 8 : end - 1, 0x80 | p->ch, n, 0, 3);
    }
}

/* ---- a tune --------------------------------------------------------------- */

static char defs_name[MAX_DEFS][32];
static char *defs_text[MAX_DEFS];
static int ndefs;

/* src with its phrases put in (theirs too, to a depth of 8), into dst; the length written. */
static size_t expand_into(char *dst, size_t cap, const char *src, int depth)
{
    size_t n = 0;
    if (depth > 8)
        die("phrases nested too deep", NULL);
    while (*src && n + 1 < cap) {
        if (*src == '$') {
            char name[32];
            int k = 0, d, times = 1;
            src++;
            while ((isalnum((unsigned char)*src) || *src == '_') && k < 31)
                name[k++] = *src++;
            name[k] = 0;
            while (*src == ' ')
                src++;
            if (*src == 'x' && isdigit((unsigned char)src[1])) {
                times = atoi(src + 1);
                src++;
                while (isdigit((unsigned char)*src))
                    src++;
            }
            for (d = 0; d < ndefs && strcmp(defs_name[d], name); d++)
                continue;
            if (d == ndefs)
                die("no such phrase", name);
            while (times-- > 0) {
                n += expand_into(dst + n, cap - n, defs_text[d], depth + 1);
                if (n + 2 >= cap)
                    die("too much music", NULL);
                dst[n++] = ' ';
            }
            continue;
        }
        dst[n++] = *src++;
    }
    dst[n] = 0;
    return n;
}

static void expand(char *dst, size_t cap, const char *src)
{
    expand_into(dst, cap, src, 0);
}

static void vlq(FILE *f, unsigned long v, long *bytes)
{
    unsigned char b[5];
    int n = 0;
    b[n++] = v & 0x7F;
    while (v >>= 7)
        b[n++] = (unsigned char)(0x80 | (v & 0x7F));
    while (n--) {
        fputc(b[n], f);
        (*bytes)++;
    }
}

static void be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

/* A track of evs[a .. b) (sorted) to f, with its header; the loop's end at `end`. */
static void track(FILE *f, int a, int b, long end)
{
    long start = ftell(f), bytes = 0, last = 0;
    unsigned char hd[8] = { 'M', 'T', 'r', 'k', 0, 0, 0, 0 };
    int i, k;
    fwrite(hd, 1, 8, f);
    for (i = a; i < b; i++) {
        vlq(f, (unsigned long)(evs[i].tick - last), &bytes);
        last = evs[i].tick;
        for (k = 0; k < evs[i].n; k++) {
            fputc(evs[i].b[k], f);
            bytes++;
        }
    }
    vlq(f, (unsigned long)(end > last ? end - last : 0), &bytes);    /* the end of the track: the loop's length */
    fputc(0xFF, f);
    fputc(0x2F, f);
    fputc(0x00, f);
    bytes += 3;
    fseek(f, start + 4, SEEK_SET);
    be32(hd, (unsigned long)bytes);
    fwrite(hd, 1, 4, f);
    fseek(f, 0, SEEK_END);
}

static long tune(const char *path, const char *mid_path, char *title, size_t title_cap)
{
    static char text[MAX_TEXT], music[MAX_TEXT], *part_music[MAX_PARTS];
    static partstate parts[MAX_PARTS];
    FILE *in = fopen(path, "r"), *out;
    char lbuf[4096], *defining = NULL;
    int nparts = 0, beats = 4, beat_note = 4, i, cur = -1, tempo = 120;
    size_t tl = 0;
    double swing = 0;
    long end = 0, size;
    unsigned char hd[14] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 0, 0, PPQ };
    file = path;
    line_no = 0;
    nevs = 0;
    ndefs = 0;
    snprintf(title, title_cap, "%s", path);
    if (!in)
        die("cannot read", path);
    memset(parts, 0, sizeof parts);
    while (fgets(lbuf, sizeof lbuf, in)) {
        char *s = lbuf, *hash, w[64];
        line_no++;
        for (hash = lbuf; *hash; hash++)               /* a comment: # at the start or after a space (f#4 is a note) */
            if (*hash == '#' && (hash == lbuf || isspace((unsigned char)hash[-1]))) {
                *hash = 0;
                break;
            }
        while (isspace((unsigned char)*s))
            s++;
        if (!*s)
            continue;
        if (sscanf(s, "%63s", w) != 1)
            continue;
        if (defining) {
            if (!strcmp(w, "enddef")) {
                defining = NULL;
                continue;
            }
            if (strlen(defining) + strlen(s) + 2 >= 16384)
                die("a phrase too long", NULL);
            strcat(defining, " ");
            strcat(defining, s);
            continue;
        }
        if (!strcmp(w, "title")) {
            char *v = s + 5;
            while (isspace((unsigned char)*v))
                v++;
            v[strcspn(v, "\r\n")] = 0;
            snprintf(title, title_cap, "%s", v);
        } else if (!strcmp(w, "tempo"))
            tempo = atoi(s + 5);
        else if (!strcmp(w, "meter")) {
            if (sscanf(s + 5, "%d/%d", &beats, &beat_note) != 2)
                die("a meter", s);
        } else if (!strcmp(w, "swing"))
            swing = atof(s + 5);
        else if (!strcmp(w, "define")) {
            if (ndefs == MAX_DEFS || sscanf(s + 6, "%31s", defs_name[ndefs]) != 1)
                die("a phrase", s);
            defs_text[ndefs] = calloc(1, 16384);
            defining = defs_text[ndefs++];
        } else if (!strcmp(w, "part")) {
            partstate *p;
            int ch = 1, prog = 0, vol = 100;
            char name[32];
            if (nparts == MAX_PARTS)
                die("too many parts", NULL);
            if (sscanf(s + 4, "%31s ch %d prog %d vol %d", name, &ch, &prog, &vol) < 2)
                die("a part (part NAME ch N prog P vol V)", s);
            cur = nparts++;
            p = &parts[cur];
            p->ch = (ch - 1) & 15;
            p->drums = ch == 10;
            p->swing = swing;
            p->bar_len = (long)beats * PPQ * 4 / beat_note;
            if (tl + 2 >= MAX_TEXT)
                die("too much music", NULL);
            text[tl++] = 0;                            /* (the part before ends here) */
            part_music[cur] = text + tl;
            text[tl] = 0;
            add(0, 0xB0 | p->ch, 7, vol, 3);           /* its volume */
            if (!p->drums)
                add(0, 0xC0 | p->ch, prog & 127, 0, 2);
        } else {
            size_t l = strlen(s);
            if (cur < 0)
                die("music outside a part", s);
            if (tl + l + 2 >= MAX_TEXT)
                die("too much music", NULL);
            memcpy(text + tl, s, l);
            tl += l;
            text[tl++] = ' ';
            text[tl] = 0;
        }
    }
    fclose(in);
    for (i = 0; i < nparts; i++) {
        /* Each part's music, its phrases put in, token by token. */
        char *s;
        partstate *p = &parts[i];
        expand(music, sizeof music, part_music[i]);
        line_no = 0;
        for (s = music; *s;) {
            /* A token by hand (token() splits chords with strtok). */
            char copy[64];
            size_t k = 0;
            while (*s && isspace((unsigned char)*s))
                s++;
            if (!*s)
                break;
            while (*s && !isspace((unsigned char)*s) && k < sizeof copy - 1)
                copy[k++] = *s++;
            copy[k] = 0;
            token(p, copy);
        }
        if (p->t > end)
            end = p->t;
    }
    for (i = 0; i < nparts; i++)
        if (parts[i].t != end) {
            char m[96];
            snprintf(m, sizeof m, "part %d is %ld ticks, the longest %ld", i + 1, parts[i].t, end);
            die("parts of different lengths", m);
        }
    qsort(evs, (size_t)nevs, sizeof evs[0], cmp_ev);
    out = fopen(mid_path, "wb");
    if (!out)
        die("cannot write", mid_path);
    hd[11] = 2;                                         /* two tracks: the tempo's, and every part's */
    fwrite(hd, 1, 14, out);
    {
        /* Track 0: the tempo and meter. */
        long start = ftell(out), bytes = 0;
        unsigned char th[8] = { 'M', 'T', 'r', 'k', 0, 0, 0, 0 };
        unsigned long us = 60000000ul / (unsigned long)tempo;
        int dd = beat_note == 8 ? 3 : beat_note == 2 ? 1 : 2;
        fwrite(th, 1, 8, out);
        fputc(0, out);
        fputc(0xFF, out);
        fputc(0x51, out);
        fputc(3, out);
        fputc((int)(us >> 16) & 255, out);
        fputc((int)(us >> 8) & 255, out);
        fputc((int)us & 255, out);
        fputc(0, out);
        fputc(0xFF, out);
        fputc(0x58, out);
        fputc(4, out);
        fputc(beats, out);
        fputc(dd, out);
        fputc(24, out);
        fputc(8, out);
        bytes = 15;
        vlq(out, (unsigned long)end, &bytes);
        fputc(0xFF, out);
        fputc(0x2F, out);
        fputc(0, out);
        bytes += 3;
        fseek(out, start + 4, SEEK_SET);
        be32(th, (unsigned long)bytes);
        fwrite(th, 1, 4, out);
        fseek(out, 0, SEEK_END);
    }
    track(out, 0, nevs, end);
    size = ftell(out);
    fclose(out);
    printf("fwtune: %s: \"%s\", %d parts, %.1f s a loop, %d events, %ld bytes\n", mid_path, title, nparts,
           end / (double)PPQ * 60.0 / tempo, nevs, size);
    for (i = 0; i < ndefs; i++)
        free(defs_text[i]);
    return size;
}

int main(int argc, char **argv)
{
    FILE *c;
    int i;
    if (argc < 4) {
        fprintf(stderr, "usage: fwtune OUT.c OUTDIR TUNE.fwt...\n");
        return 2;
    }
    c = fopen(argv[1], "w");
    if (!c) {
        fprintf(stderr, "fwtune: cannot write %s\n", argv[1]);
        return 1;
    }
    fprintf(c, "/* music_gen.c - made by tools/fwtune.c from data/music: do not edit. */\n#include \"../../game/src/music.h\"\n\n");
    for (i = 3; i < argc; i++) {
        char mid[512], title[128], name[64];
        const char *base = strrchr(argv[i], '/');
        FILE *m;
        long size, k;
        snprintf(name, sizeof name, "%s", base ? base + 1 : argv[i]);
        name[strcspn(name, ".")] = 0;
        snprintf(mid, sizeof mid, "%s/%s.MID", argv[2], name);
        for (k = 0; mid[k]; k++)
            if (k > (long)strlen(argv[2]))
                mid[k] = (char)toupper((unsigned char)mid[k]);
        size = tune(argv[i], mid, title, sizeof title);
        m = fopen(mid, "rb");
        fprintf(c, "static const unsigned char tune%d[%ld] = {", i - 3, size);
        for (k = 0; k < size; k++)
            fprintf(c, "%s%d,", k % 24 ? "" : "\n   ", fgetc(m));
        fprintf(c, "\n};\n\n");
        fclose(m);
    }
    fprintf(c, "const music_tune music_tunes[] = {\n");
    for (i = 3; i < argc; i++) {
        const char *base = strrchr(argv[i], '/');
        char name[64];
        snprintf(name, sizeof name, "%s", base ? base + 1 : argv[i]);
        name[strcspn(name, ".")] = 0;
        fprintf(c, "    { \"%s\", tune%d, sizeof tune%d },\n", name, i - 3, i - 3);
    }
    fprintf(c, "};\nconst int music_ntunes = %d;\n", argc - 3);
    fclose(c);
    return 0;
}
