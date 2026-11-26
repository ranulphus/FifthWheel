/* opl.h - the Sound Blaster's FM chip (Yamaha OPL2 YM3812, or OPL3 YMF262
 * in its OPL2 mode): nine two-operator channels, or six and five drums in
 * rhythm mode, written register by register. On DOS the real chip at the
 * AdLib ports (388h, which every Sound Blaster decodes), found by its timer
 * test; none (GLOS's virtual Sound Blaster, a PCI card without FM, a GLOS
 * app): quietly nothing. Elsewhere (Linux, headless) an emulation of our
 * own, close enough to hear and test the music by, mixed by dgk. */
#ifndef FW_OPL_H
#define FW_OPL_H

#include "dgk/base.h"

enum { OPL_NONE, OPL_EMULATED, OPL_CHIP2, OPL_CHIP3 };

int  opl_open(void);                 /* OPL_*: what plays the registers */
void opl_write(int reg, int val);    /* an OPL2 register (00h-F5h) */
void opl_reset(void);                /* every key off, every register 0 (silent) */
void opl_close(void);                /* reset, and the emulation's stream off */

#endif
