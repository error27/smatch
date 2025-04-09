#include "check_debug.h"

unsigned long frob(void);

void test(void)
{
	unsigned int a, b;
	unsigned short x;

	a = frob();
	if (a > 255)
		return;
	if (a & 1) {
		if (a & 2)
			__smatch_bits(a);
		b = a;
	} else {
		if (a & 0xf0)
			return;
		__smatch_bits(a);
		b = 3;
	}
	__smatch_bits(b);
	a = x = frob();
	__smatch_bits(a);
}

/*
 * check-name: smatch bits #1
 * check-command: smatch -I.. sm_bits1.c
 *
 * check-output-start
sm_bits1.c:15 test() bit info 'a': definitely set 0x3.  possibly set 0xff.
sm_bits1.c:20 test() bit info 'a': definitely set 0x0.  possibly set 0xe.
sm_bits1.c:23 test() bit info 'b': definitely set 0x1.  possibly set 0xff.
sm_bits1.c:25 test() bit info 'a': definitely set 0x0.  possibly set 0xffff.
 * check-output-end
 */
