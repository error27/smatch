#include "check_debug.h"

int zero(void)
{
	return 0;
}


int main(unsigned int x, unsigned int y)
{
	if (zero())
		__smatch_states("smatch_impossible_return");
	else
		__smatch_states("smatch_impossible_return");
}

/*
 * check-name: smatch impossible #1
 * check-command: smatch -I.. sm_impossible1.c
 *
 * check-output-start
sm_impossible1.c:12 main() [smatch_impossible_return] impossible (nil) = 'impossible'
sm_impossible1.c:14 main() no states found for 'smatch_impossible_return'
sm_impossible1.c:14 main() smatch_impossible_return: no states
 * check-output-end
 */
