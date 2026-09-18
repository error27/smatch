#include "check_debug.h"

static unsigned long array_index_mask_nospec(unsigned long index,
					      unsigned long size);

void frob(unsigned int x)
{
	if (array_index_mask_nospec(x, 4))
		__smatch_implied(x);
	else
		__smatch_implied(x);
}

/*
 * check-name: smatch: split database return states
 * check-command: validation/smatch_split_return_test.sh sm_split_return1.c
 * check-known-to-fail
 *
 * check-output-start
return: 0
return: u64max
sm_split_return1.c:9 frob() implied: x = '0-3'
sm_split_return1.c:11 frob() implied: x = '4-u32max'
 * check-output-end
 */
