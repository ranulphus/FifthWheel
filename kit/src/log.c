/* log.c - one-line messages: COM1 on DOS (when asked), stderr elsewhere. */
#include "dgk/log.h"
#include "dgk/base.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DGK_DOS
#include <pc.h>

static int serial = -1;                  /* -1: not decided yet */

static void com1_put(char c)
{
    long spin = 0;
    while (!(inportb(0x3FD) & 0x20) && ++spin < 100000L)
        continue;
    outportb(0x3F8, (unsigned char)c);
}

static void com1_open(void)
{
    outportb(0x3FB, 0x80);               /* 115200 8N1, no interrupts */
    outportb(0x3F8, 1);
    outportb(0x3F9, 0);
    outportb(0x3FB, 0x03);
    outportb(0x3FA, 0xC7);
    outportb(0x3FC, 0x03);
}

void dgk_log_serial(int on)
{
    if (on && serial != 1)
        com1_open();
    serial = on;
}
#else
void dgk_log_serial(int on)
{
    DGK_UNUSED(on);
}
#endif

void dgk_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
#ifdef DGK_DOS
    if (serial < 0) {
        const char *e = getenv("DGK_SERIAL");
        dgk_log_serial(e && *e == '1');
    }
    if (serial) {
        const char *p;
        for (p = line; *p; p++)
            com1_put(*p);
        com1_put('\r');
        com1_put('\n');
    }
#else
    fprintf(stderr, "%s\n", line);
#endif
}
