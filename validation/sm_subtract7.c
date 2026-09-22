#include "check_debug.h"

int frob(unsigned int remained, unsigned int max_count)
{
	remained &= 0xff;

	if (remained >= max_count)
		return 0;

	__smatch_implied(remained);
	__smatch_implied(max_count);
	__smatch_implied(max_count - remained);
	__smatch_real_absolute(max_count - remained);

	return 0;
}

/*
 * check-name: smatch: subtract #7
 * check-command: ./smatch -I.. sm_subtract7.c
 *
 * check-output-start
sm_subtract7.c:10 frob() implied: remained = '0-255'
sm_subtract7.c:11 frob() implied: max_count = '1-u32max'
sm_subtract7.c:12 frob() implied: max_count - remained = '1-4294967294'
sm_subtract7.c:13 frob() real absolute: max_count - remained = '1-4294967294'
 * check-output-end
 */
