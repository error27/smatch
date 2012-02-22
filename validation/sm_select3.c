#include "check_debug.h"

int a, b, c;
int func(void)
{
	if (a ? b : c)
		__smatch_value("a");

	__smatch_note("Test #1 a ? 1 : c");
	if (a ? 1 : c) {	
		__smatch_value("a");
		__smatch_value("c");
		if (!a)
			__smatch_value("c");
		if (!c)
			__smatch_value("a");
	} else {
		__smatch_value("a");
		__smatch_value("c");
	}

	__smatch_note("Test #2 a ? 0 : c");
	if (a ? 0 : c) {	
		__smatch_value("a");
		__smatch_value("c");
		if (!a)
			__smatch_value("c");
	} else {
		__smatch_value("a");
		__smatch_value("c");
		if (!a)
			__smatch_value("c");
		if (!c)
			__smatch_value("a");
	}

	__smatch_note("Test #3 a ? b : 1");
	if (a ? b : 1) {	
		__smatch_value("a");
		__smatch_value("b");
		if (!a)
			__smatch_value("b");
		if (!b)
			__smatch_value("a");
	} else {
		__smatch_value("a");
		__smatch_value("b");
		if (!b)
			__smatch_value("a");
	}

	__smatch_note("Test #2 a ? b : 0");
	if (a ? b : 0) {	
		__smatch_value("a");
		__smatch_value("b");
	} else {
		__smatch_value("a");
		__smatch_value("b");
		if (a)
			__smatch_value("b");
		if (b)
			__smatch_value("a");
	}
}


/*
 * check-name: Ternary Conditions #3
 * check-command: smatch -I.. sm_select3.c
 *
 * check-output-start
sm_select3.c:7 func(3) a = unknown
sm_select3.c:9 func(5) Test #1 a ? 1 : c
sm_select3.c:11 func(7) a = unknown
sm_select3.c:12 func(8) c = unknown
sm_select3.c:14 func(10) c = min-(-1),1-max
sm_select3.c:16 func(12) a = min-(-1),1-max
sm_select3.c:18 func(14) a = 0
sm_select3.c:19 func(15) c = 0
sm_select3.c:22 func(18) Test #2 a ? 0 : c
sm_select3.c:24 func(20) a = 0
sm_select3.c:25 func(21) c = min-(-1),1-max
sm_select3.c:27 func(23) c = min-(-1),1-max
sm_select3.c:29 func(25) a = unknown
sm_select3.c:30 func(26) c = unknown
sm_select3.c:32 func(28) c = 0
sm_select3.c:34 func(30) a = unknown
sm_select3.c:37 func(33) Test #3 a ? b : 1
sm_select3.c:39 func(35) a = unknown
sm_select3.c:40 func(36) b = unknown
sm_select3.c:42 func(38) b = unknown
sm_select3.c:44 func(40) a = 0
sm_select3.c:46 func(42) a = min-(-1),1-max
sm_select3.c:47 func(43) b = 0
sm_select3.c:49 func(45) a = min-(-1),1-max
sm_select3.c:52 func(48) Test #2 a ? b : 0
sm_select3.c:54 func(50) a = min-(-1),1-max
sm_select3.c:55 func(51) b = min-(-1),1-max
sm_select3.c:57 func(53) a = unknown
sm_select3.c:58 func(54) b = unknown
sm_select3.c:60 func(56) b = 0
sm_select3.c:62 func(58) a = 0
 * check-output-end
 */
