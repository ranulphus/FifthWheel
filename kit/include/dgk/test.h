/* dgk/test.h - test reporting. On DOS under -test these are the HX- lines
 * Loop A reads on COM1 (the harness's guest shim); on the desktop they are
 * lines on stdout in the same form, for the host test scripts. */
#ifndef DGK_TEST_H
#define DGK_TEST_H

int  dgk_test_active(void);
void dgk_test_check(const char *name, int ok, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int  dgk_test_snapshot(const char *name);   /* the frame just drawn, before the swap */

#endif
