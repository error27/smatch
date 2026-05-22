#include "check_debug.h"

void frob(void);

#define min(a, b) ((a) < (b) ? (a) : (b))

void func(void)
{
	int i;
	int val;

	for (i = 0; i < 10; i++) {
		val = min(5, i);
		__smatch_value("val");
		if (frob())
			break;
	}

	i++;
	__smatch_value("i");
	val = min(100, i);
	__smatch_value("val");

	for (i = 0; i < 10; i++) {
		if (frob())
			break;
	}

	val = min(100, i);
	__smatch_value("val");
}

/*
 * check-name: assigning select statements
 * check-command: smatch -I.. sm_select_assign.c
 *
 * check-output-start
sm_select_assign.c:14 func() val = 0-5
sm_select_assign.c:20 func() i = 1-11
sm_select_assign.c:22 func() val = 1-11
sm_select_assign.c:30 func() val = 0-10
 * check-output-end
 */
