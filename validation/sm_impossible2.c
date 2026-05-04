#include "check_debug.h"

int one(void)
{
	return 1;
}


int main(unsigned int x, unsigned int y)
{
	if (one())
		__smatch_states("smatch_impossible_return");
	else
		__smatch_states("smatch_impossible_return");
}

/*
 * check-name: smatch impossible #2
 * check-command: smatch -I.. sm_impossible2.c
 *
 * check-output-start
sm_impossible2.c:12 main() no states found for 'smatch_impossible_return'
sm_impossible2.c:12 main() smatch_impossible_return: no states
sm_impossible2.c:14 main() [smatch_impossible_return] impossible (nil) = 'impossible'
 * check-output-end
 */
