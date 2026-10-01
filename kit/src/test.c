/* test.c - test reporting (see dgk/test.h). */
#include "dgk/test.h"
#include "dgk/base.h"
#include "dgk/log.h"
#include "plat.h"
#include <stdarg.h>
#include <stdio.h>
#ifdef DGK_DOS
#include "hx.h"
#endif

static int active, failures;

void dgk_test_begin(const char *name, int noexit)
{
    active = 1;
#ifdef DGK_DOS
    {
        char *argv[3];
        argv[0] = (char *)name;
        argv[1] = (char *)"--noexit";
        argv[2] = NULL;
        dgk_log_serial(1);
        hx_init(noexit ? 2 : 1, argv, name);
    }
#else
    DGK_UNUSED(noexit);
    printf("HX-START %s\n", name);
#endif
}

int dgk_test_active(void)
{
    return active;
}

void dgk_test_check(const char *name, int ok, const char *fmt, ...)
{
    char msg[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    failures += !ok;
    if (!active) {
        if (!ok)
            dgk_log("FW-FAIL %s %s", name, msg);
        return;
    }
#ifdef DGK_DOS
    hx_test(name, ok, "%s", msg);
#else
    printf("HX-TEST %s %s %s\n", name, ok ? "PASS" : "FAIL", msg);
#endif
}

int dgk_test_snapshot(const char *name)
{
    int r = plat_snapshot(name);
    dgk_test_check("snapshot", r == 0, "%s", name);
    return r;
}

int dgk_test_end(void)
{
#ifdef DGK_DOS
    hx_done(failures ? 1 : 0);                 /* ends the run (86Box's unit tester) */
#else
    printf("HX-DONE %d\n", failures ? 1 : 0);
#endif
    return failures ? 1 : 0;
}
