#include "check_debug.h"

void frob();

#define min(a, b) ((a) < (b) ? (a) : (b))

void func(void)
{
	int i;
	int val;

	for (i = 0; i < 10; i++) {
		val = min(5, i);
		__smatch_value("val");
	}

	i++;
	__smatch_value("i");
	val = min(100, i);
	__smatch_value("val");

	for (i = 0; i < 10; i++)
		frob();

	val = min(100, i);
	__smatch_value("val");
}
/*
 * check-name: assigning select statements
 * check-command: smatch -I.. sm_select_assign.c
 *
 * check-output-start
sm_select_assign.c +14 func(7) val = 0-5
sm_select_assign.c +18 func(11) i = 11-max
sm_select_assign.c +20 func(13) val = 11-100
sm_select_assign.c +26 func(19) val = 10
 * check-output-end
 */
