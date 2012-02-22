#include "check_debug.h"

int aaa;

void func (void)
{
	if (aaa > 0 && aaa < 100) {
		__smatch_value("aaa");
	} else {
		__smatch_value("aaa");
	}
	if (aaa > 0 && aaa < 100 && aaa < 10) {
		__smatch_value("aaa");
	} else {
		if (aaa != 42)
			__smatch_value("aaa");
	}
}
/*
 * check-name: Compound Conditions #3
 * check-command: smatch -I.. sm_compound_conditions3.c
 *
 * check-output-start
sm_compound_conditions3.c:8 func() aaa = 1-99
sm_compound_conditions3.c:10 func() aaa = min-0,100-max
sm_compound_conditions3.c:13 func() aaa = 1-9
sm_compound_conditions3.c:16 func() aaa = min-0,10-41,43-max
 * check-output-end
 */
