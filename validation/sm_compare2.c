#include "check_debug.h"

int a, b, c;

int main(void)
{
	if (a < 4)
		return 1;
	if (a > 10)
		return 2;
	__smatch_value("a");

	if (b < 3)
		return 3;
	if (b > 15)
		return 4;
	__smatch_value("b");

	if (b > a) {
		__smatch_value("a");
		__smatch_value("b");
	} else {
		__smatch_value("a");
		__smatch_value("b");
	}
	return 5;
}

/*
 * check-name: Smatch Comparison #2
 * check-command: smatch -I.. sm_compare2.c
 *
 * check-output-start
sm_compare2.c:11 main(6) a = 4-10
sm_compare2.c:17 main(12) b = 3-15
sm_compare2.c:20 main(15) a = 4-10
sm_compare2.c:21 main(16) b = 5-15
sm_compare2.c:23 main(18) a = 4-10
sm_compare2.c:24 main(19) b = 3-10
 * check-output-end
 */
