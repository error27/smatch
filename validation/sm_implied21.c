#include "check_debug.h"

int three(void)
{
	return 3;
}

void func(void)
{
	__smatch_known(three());
}


/*
 * check-name: smatch implied #21
 * check-command: smatch -I.. sm_implied21.c
 *
 * check-output-start
sm_implied21.c:10 func() known: 'three()' = '3'.  implied = '3'
 * check-output-end
 */
