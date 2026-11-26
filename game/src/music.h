/* music.h - Fifth Wheel's music: tunes of its own (data/music, made into
 * standard MIDI files by tools/fwtune.c and built into the game), played as
 * General MIDI through dgk's music (dgk/midi.h: the Sound Blaster's FM chip
 * with GLOS's instruments, GLOS's MIDI ports as a GLOS app, an emulated OPL3
 * off DOS). A tune loops; the driving tunes take turns, each played once.
 * Quietly nothing where dgk finds nothing to play on, or with -nosound. Timed
 * by the game's clock (in a -fixed test, its ticks: the same messages every
 * run); music_update sends what falls due soon, so the game calls it at every
 * yield. */
#ifndef FW_MUSIC_H
#define FW_MUSIC_H

typedef struct music_tune {
    const char *name;
    const unsigned char *data;               /* a standard MIDI file */
    unsigned int size;
} music_tune;

extern const music_tune music_tunes[];       /* music_gen.c: title, garage, then the driving tunes */
extern const int music_ntunes;

enum { MUSIC_OFF, MUSIC_TITLE, MUSIC_GARAGE, MUSIC_DRIVING };

void music_init(void);
void music_want(int what);                   /* MUSIC_*: the same again carries on */
int  music_play(const char *name);           /* that tune alone, looped (a test): 0 if there is none */
void music_volume(int level);                /* 0 (silent) .. 10 */
void music_update(void);
void music_shutdown(void);                   /* silent, the chip reset */
const char *music_playing(void);             /* the tune's name, or NULL */
double music_loop_s(void);                   /* its length, seconds (0: none) */

#endif
