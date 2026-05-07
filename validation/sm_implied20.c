#include "check_debug.h"

int check(void);

void func(int *x)
{
	int success = 0;
	int i;

	for (i = 0; i < 10; i++) {
		if (check()) {
			success = 1;
			break;
		}
	}
	__smatch_implied(i);
	if (success)
		__smatch_implied(i);
	else
		__smatch_implied(i);
}

/*
 * check-name: smatch: implied #20
 * check-command: smatch -I.. sm_implied20.c
 *
 * check-output-start
sm_implied20.c:16 func() implied: i = '0-10'
sm_implied20.c:18 func() implied: i = '0-9'
sm_implied20.c:20 func() implied: i = '10'
 * check-output-end
 */
