#include "check_debug.h"

int x;
void func(void)
{

	if (x < 1)
		__smatch_value("x");
	else
		__smatch_value("x");

	if (12 < x)
		__smatch_value("x");
	else
		__smatch_value("x");

	if (x <= 23)
		__smatch_value("x");
	else
		__smatch_value("x");

	if (34 <= x)
		__smatch_value("x");
	else
		__smatch_value("x");

	if (x >= 45)
		__smatch_value("x");
	else
		__smatch_value("x");

	if (56 >= x)
		__smatch_value("x");
	else
		__smatch_value("x");

	if (x > 67)
		__smatch_value("x");
	else
		__smatch_value("x");

	if (78 > x)
		__smatch_value("x");
	else
		__smatch_value("x");

	if (89 == x)
		__smatch_value("x");
	else
		__smatch_value("x");

	if (100 != x)
		__smatch_value("x");
	else
		__smatch_value("x");

	return;
}
/*
 * check-name: smatch range comparison
 * check-command: smatch -I.. sm_range3.c
 *
 * check-output-start
sm_range3.c +8 func(4) x = min-0
sm_range3.c +10 func(6) x = 1-max
sm_range3.c +13 func(9) x = 13-max
sm_range3.c +15 func(11) x = min-12
sm_range3.c +18 func(14) x = min-23
sm_range3.c +20 func(16) x = 24-max
sm_range3.c +23 func(19) x = 34-max
sm_range3.c +25 func(21) x = min-33
sm_range3.c +28 func(24) x = 45-max
sm_range3.c +30 func(26) x = min-44
sm_range3.c +33 func(29) x = min-56
sm_range3.c +35 func(31) x = 57-max
sm_range3.c +38 func(34) x = 68-max
sm_range3.c +40 func(36) x = min-67
sm_range3.c +43 func(39) x = min-77
sm_range3.c +45 func(41) x = 78-max
sm_range3.c +48 func(44) x = 89
sm_range3.c +50 func(46) x = min-88,90-max
sm_range3.c +53 func(49) x = min-99,101-max
sm_range3.c +55 func(51) x = 100
 * check-output-end
 */
