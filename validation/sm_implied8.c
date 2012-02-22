#include "check_debug.h"

void frob();

int x;
void func (void)
{
	int test, test2;

	if (({int test = !!x; frob(); frob(); frob(); test;}))
		__smatch_value("x");
	else
		__smatch_value("x");
	if (test)
		__smatch_value("x");
	if (({test2 = !(x == 3); frob(); frob(); frob(); test2;}))
		__smatch_value("x");
	else
		__smatch_value("x");
	test = !!(x == 10);
	if (!test)
		__smatch_value("x");
	__smatch_value("x");
}
/*
 * check-name: smatch implied #8
 * check-command: smatch -I.. sm_implied8.c
 *
 * check-output-start
sm_implied8.c:11 func() x = min-(-1),1-max
sm_implied8.c:13 func() x = 0
sm_implied8.c:15 func() x = unknown
sm_implied8.c:17 func() x = min-2,4-max
sm_implied8.c:19 func() x = 3
sm_implied8.c:22 func() x = min-9,11-max
sm_implied8.c:23 func() x = unknown
 * check-output-end
 */
