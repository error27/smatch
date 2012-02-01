#include "check_debug.h"

int aaa;
int x, y, z;

void func (void)
{
	aaa = 0;
	if (y)
		aaa = 1;
	if (x)
		aaa = 2;

	if (x) {
		__smatch_value("aaa");
		if (y)
			__smatch_value("aaa");
		else
			__smatch_value("aaa");
	}
	if (!x) {		
		__smatch_value("aaa");
		if (y)		
			__smatch_value("aaa");
		else
			__smatch_value("aaa");
	}
	if (y) {
		__smatch_value("aaa");
		if (x)
			__smatch_value("aaa");
		else
			__smatch_value("aaa");
	}
	if (!y) {
		__smatch_value("aaa");
		if (x)		
			__smatch_value("aaa");
		else
			__smatch_value("aaa");
	}
	if (x && y)
		__smatch_value("aaa");
	if (x || y)
		__smatch_value("aaa");
	else
		__smatch_value("aaa");
	if (!x && !y)
		__smatch_value("aaa");
}
/*
 * check-name: Compound Conditions #2
 * check-command: smatch -I.. sm_compound_conditions2.c
 *
 * check-output-start
sm_compound_conditions2.c:15 func(9) aaa = 2
sm_compound_conditions2.c:17 func(11) aaa = 2
sm_compound_conditions2.c:19 func(13) aaa = 2
sm_compound_conditions2.c:22 func(16) aaa = 0-1
sm_compound_conditions2.c:24 func(18) aaa = 1
sm_compound_conditions2.c:26 func(20) aaa = 0
sm_compound_conditions2.c:29 func(23) aaa = 1-2
sm_compound_conditions2.c:31 func(25) aaa = 2
sm_compound_conditions2.c:33 func(27) aaa = 1
sm_compound_conditions2.c:36 func(30) aaa = 0,2
sm_compound_conditions2.c:38 func(32) aaa = 2
sm_compound_conditions2.c:40 func(34) aaa = 0
sm_compound_conditions2.c:43 func(37) aaa = 2
sm_compound_conditions2.c:45 func(39) aaa = 1-2
sm_compound_conditions2.c:47 func(41) aaa = 0
sm_compound_conditions2.c:49 func(43) aaa = 0
 * check-output-end
 */
