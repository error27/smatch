#include "check_debug.h"

static int fls(unsigned int x);
static int fls64(unsigned long long x);

void test(unsigned int x, unsigned long long y)
{
	if (x == 0)
		__smatch_implied(fls(x));
	if (x == 8)
		__smatch_implied(fls(x));

	if (x < 8 || x > 15)
		return;
	__smatch_implied(fls(x));

	if (y < (1ULL << 40) || y > (1ULL << 41))
		return;
	__smatch_implied(fls64(y));
}

/*
 * check-name: smatch: kernel fls ranges
 * check-command: smatch -p=kernel -I.. sm_fls.c
 *
 * check-output-start
sm_fls.c:9 test() implied: fls(x) = '0'
sm_fls.c:11 test() implied: fls(x) = '4'
sm_fls.c:15 test() implied: fls(x) = '1-4'
sm_fls.c:19 test() implied: fls64(y) = '1-42'
 * check-output-end
 */
