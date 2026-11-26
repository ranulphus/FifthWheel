/* music.h - Fifth Wheel's music on the Sound Blaster's FM chip (opl.h): tunes
 * of its own (data/music, made into standard MIDI files by tools/fwtune.c and
 * built into the game), played by a small set of FM instruments of its own
 * (one for each General MIDI family the tunes use: piano, vibes, clarinet,
 * brass, bass ...) on six of the chip's channels, the drums in its rhythm
 * mode. A tune loops; the driving tunes take turns (each played twice).
 * Quietly nothing without a chip (opl.h) or with -nosound. Timed by the
 * game's clock (in a -fixed test, its ticks: the same notes every run);
 * music_update plays what is due, so the game calls it at every yield. */
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

#endif
