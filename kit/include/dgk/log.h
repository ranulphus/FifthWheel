/* dgk/log.h - one-line messages: stderr on the desktop; on DOS, COM1
 * (115200 8N1, polled) when the DGK_SERIAL environment variable is 1 or a
 * test is running, where Loop A reads them. Lines the harness looks for
 * start with a tag ("FW-STAT ..."). */
#ifndef DGK_LOG_H
#define DGK_LOG_H

void dgk_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void dgk_log_serial(int on);          /* force COM1 on or off (DOS) */

#endif
