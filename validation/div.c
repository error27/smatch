#include <limits.h>

static int xd = 1 / 0;
static int xl = 1L / 0;
static int xll = 1LL / 0;

static int yd = INT_MIN / -1;
static long yl = LONG_MIN / -1;
static long long yll = LLONG_MIN / -1;

static int zd = INT_MIN % -1;
static long zl = LONG_MIN % -1;
static long long zll = LLONG_MIN % -1;

/*
 * check-name: division constants
 *
 * check-error-start
div.c:3:19: warning: division by zero
div.c:4:20: warning: division by zero
div.c:5:22: warning: division by zero
div.c:7:25: warning: constant integer operation overflow
div.c:8:27: warning: constant integer operation overflow
div.c:9:34: warning: constant integer operation overflow
div.c:11:25: warning: constant integer operation overflow
div.c:12:27: warning: constant integer operation overflow
div.c:13:34: warning: constant integer operation overflow
 * check-error-end
 */
