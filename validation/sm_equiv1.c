#include "check_debug.h"

int *something();

int *one;
int *two;
int func(void)
{
	one = something();
	two = one;

	if (two == 1) {
		__smatch_value("one");
		__smatch_value("two");
	}
	__smatch_value("one");
	__smatch_value("two");
	if (one == 2) {
		__smatch_value("one");
		__smatch_value("two");
	}
	__smatch_value("one");
	__smatch_value("two");
	return 0;
}
/*
 * check-name: smatch equivalent variables #1
 * check-command: smatch -I.. sm_equiv1.c
 *
 * check-output-start
sm_equiv1.c +13 func(6) one = 1
sm_equiv1.c +14 func(7) two = 1
sm_equiv1.c +16 func(9) one = min-max
sm_equiv1.c +17 func(10) two = min-max
sm_equiv1.c +19 func(12) one = 2
sm_equiv1.c +20 func(13) two = 2
sm_equiv1.c +22 func(15) one = min-max
sm_equiv1.c +23 func(16) two = min-max
 * check-output-end
 */
