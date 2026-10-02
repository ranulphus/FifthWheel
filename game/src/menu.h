/* menu.h - the title screen (the tractor turning in the showroom under the
 * name: DRIVE, GARAGE, OPTIONS, JOYSTICK SET-UP, QUIT), the pause menu (Esc
 * while driving: RESUME, OPTIONS, JOYSTICK SET-UP, QUIT) and the options
 * (the detail preset, the screen size from the next start, vsync, volume,
 * the governor; kept in FWHEEL.CFG). Up and down choose, left and right
 * change, Enter picks, Esc goes back. Each step logs an FW-MENU line. */
#ifndef FW_MENU_H
#define FW_MENU_H

#include "dgk/base.h"

enum { SCREEN_PLAY, SCREEN_TITLE, SCREEN_OPTIONS, SCREEN_PAUSE };

#define MENU_MODES 16

typedef struct menu {
    int screen, back;               /* the screen shown; where the options go back to */
    int cursor;
    float spin, spin_prev;          /* the title's turntable */
    int modes[MENU_MODES][2], nmodes, mode;   /* screen sizes on offer; the one chosen */
    int dirty;                      /* options changed: saved on leaving */
    int played;                     /* driving has begun (FW-PLAY logged) */
} menu;

struct game;
void menu_start(struct game *g, int title);   /* the title, or straight to driving */
void menu_open(struct game *g, int screen);
/* One tick of the screen shown: 1 while a menu is up (the world waits). */
int  menu_tick(struct game *g);
/* Draws the screen if it is a menu over the showroom (the title, or the
 * options from it): 1 if it did. */
int  menu_draw_scene(struct game *g, float alpha);
void menu_draw_overlay(struct game *g);     /* the pause menu or the options, over what is drawn */

#endif
