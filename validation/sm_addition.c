#include "check_debug.h"

int frob(int a, int b, int c, int d, int e, int f)
{
	if (a < -10 || a > 10)
		return 0;
	if (b) {} // force a state

	if (d < -2 || !d || d > 2)
		return 0;
	if (e < 2 || e > 5)
		return 0;
	if (f < -5 || f > -2)
		return 0;

	__smatch_note("test: unknown");
	__smatch_implied(a + b);
	__smatch_note("test: absolute unknown");
	__smatch_real_absolute(a + b);
	__smatch_real_absolute(a + c);
	__smatch_implied(b + a);
	__smatch_note("test: zero tests");
	__smatch_implied(0 + a);
	__smatch_implied(0 + b);
	__smatch_implied(0 + c);
	__smatch_note("test: (-10)-10 + (-2)-2 = (-12)-12");
	__smatch_implied(a + d);
	__smatch_note("test: unknown");
	__smatch_implied(b + d);
	__smatch_note("test: (-10)-10 + 2-5 = (-8)-15");
	__smatch_implied(a + e);
	__smatch_note("test: (-2)-2 + 2-5 = 0-7");
	__smatch_implied(d + e);
	__smatch_note("test: (-5)-(-2) + 2-5 = (-3)-3");
	__smatch_implied(f + e);
	__smatch_note("test: (-5)-(-2) + (-10)-10 = (-15)-8");
	__smatch_implied(f + a);
	__smatch_note("test: (-5)-(-2) + (-2)-(-1),1-2 = (-7)-0");
	__smatch_implied(f + d);
	__smatch_note("test: 2-5 + (-5)-(-2) = (-3)-3");
	__smatch_implied(e + f);
	__smatch_note("test: (-2)-(-1),1-2 + 2 = 0-1,3-4");
	__smatch_implied(d + 2);

	return 0;
}

/*
 * check-name: smatch: addition
 * check-command: ./smatch -I.. sm_addition.c
 *
 * check-output-start
sm_addition.c:16 frob() test: unknown
sm_addition.c:17 frob() implied: a + b = 's32min-s32max'
sm_addition.c:18 frob() test: absolute unknown
sm_addition.c:19 frob() real absolute: a + b = 's32min-s32max'
sm_addition.c:20 frob() real absolute: a + c = 's32min-s32max'
sm_addition.c:21 frob() implied: b + a = 's32min-s32max'
sm_addition.c:22 frob() test: zero tests
sm_addition.c:23 frob() implied: 0 + a = '(-10)-10'
sm_addition.c:24 frob() implied: 0 + b = 's32min-s32max'
sm_addition.c:25 frob() implied: 0 + c = 's32min-s32max'
sm_addition.c:26 frob() test: (-10)-10 + (-2)-2 = (-12)-12
sm_addition.c:27 frob() implied: a + d = '(-12)-12'
sm_addition.c:28 frob() test: unknown
sm_addition.c:29 frob() implied: b + d = 's32min-s32max'
sm_addition.c:30 frob() test: (-10)-10 + 2-5 = (-8)-15
sm_addition.c:31 frob() implied: a + e = '(-8)-15'
sm_addition.c:32 frob() test: (-2)-2 + 2-5 = 0-7
sm_addition.c:33 frob() implied: d + e = '0-7'
sm_addition.c:34 frob() test: (-5)-(-2) + 2-5 = (-3)-3
sm_addition.c:35 frob() implied: f + e = '(-3)-3'
sm_addition.c:36 frob() test: (-5)-(-2) + (-10)-10 = (-15)-8
sm_addition.c:37 frob() implied: f + a = '(-15)-8'
sm_addition.c:38 frob() test: (-5)-(-2) + (-2)-(-1),1-2 = (-7)-0
sm_addition.c:39 frob() implied: f + d = '(-7)-0'
sm_addition.c:40 frob() test: 2-5 + (-5)-(-2) = (-3)-3
sm_addition.c:41 frob() implied: e + f = '(-3)-3'
sm_addition.c:42 frob() test: (-2)-(-1),1-2 + 2 = 0-1,3-4
sm_addition.c:43 frob() implied: d + 2 = '0-1,3-4'
 * check-output-end
 */
