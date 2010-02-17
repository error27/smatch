#include "check_debug.h"

int x;
void func (void)
{
	int test;
	int test2;

	test = !!x;
	if (test)
		__smatch_print_value("x");
	else
		__smatch_print_value("x");
	test2 = !(x == 3);
	if (test2)
		__smatch_print_value("x");
	else
		__smatch_print_value("x");
	__smatch_print_value("x");
}
/*
 * check-name: smatch implied #8
 * check-command: smatch -I.. sm_implied8.c
 *
 * check-output-start
sm_implied8.c +11 func(7) x = min-(-1),1-max
sm_implied8.c +13 func(9) x = 0
sm_implied8.c +16 func(12) x = min-2,4-max
sm_implied8.c +18 func(14) x = 3
sm_implied8.c +19 func(15) x = min-max
 * check-output-end
 */
