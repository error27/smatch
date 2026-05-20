#include "check_debug.h"

enum my_enum {
	zero,
	one,
	two,
	minus_one = -1,
};

void func(unsigned long a)
{
	enum my_enum b;

	b = a;
	if (b <= minus_one || b > two) {
		__smatch_implied(b);
		return;
	}
	__smatch_implied(b);
}

/*
 * check-name: smatch: extra SSA #1
 * check-command: ./smatch -I.. sm_essa1.c
 *
 * check-output-start
sm_essa1.c:16 func() implied: b = 's32min-(-1),3-s32max'
sm_essa1.c:19 func() implied: b = '0-2'
 * check-output-end
 */
