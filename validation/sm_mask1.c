#include "check_debug.h"

void func(int a, int b, int c, int d, int e)
{
	if (a < 65)
		return;
	if (b < 0 || b > 7)
		return;
	if (c < 7 || c > 17)
		return;
	if (d < 0)
		return;

	e &= 0xf0;

	__smatch_implied(a);
	__smatch_implied(a & ~7);
	__smatch_implied(~7 & a);
	__smatch_implied(b & ~7);
	__smatch_implied(c & ~7);
	__smatch_implied(d & 0xff);
	__smatch_implied(d & 0xf0);
	__smatch_implied(d & e);
	__smatch_implied(d & (unsigned char)a);
	__smatch_implied(b & (unsigned char)a);
	__smatch_implied(c & (unsigned char)a);
}

/*
 * check-name: smatch: mask #1
 * check-command: ./smatch -I.. sm_mask1.c
 *
 * check-output-start
sm_mask1.c:16 func() implied: a = '65-s32max'
sm_mask1.c:17 func() implied: a & ~7 = '0,64-s32max'
sm_mask1.c:18 func() implied: ~7 & a = '0,64-s32max'
sm_mask1.c:19 func() implied: b & ~7 = '0'
sm_mask1.c:20 func() implied: c & ~7 = '0,8-16'
sm_mask1.c:21 func() implied: d & 255 = '0-255'
sm_mask1.c:22 func() implied: d & 240 = '0,16-240'
sm_mask1.c:23 func() implied: d & e = '0,16-240'
sm_mask1.c:24 func() implied: d & a = '0-255'
sm_mask1.c:25 func() implied: b & a = '0-7'
sm_mask1.c:26 func() implied: c & a = '0-17'
 * check-output-end
 */
