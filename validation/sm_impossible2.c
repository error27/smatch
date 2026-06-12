#include "check_debug.h"

int main(unsigned int x, unsigned int y)
{

	if (x >= 0)
		__smatch_states("smatch_impossible_return");
	else
		__smatch_states("smatch_impossible_return");
}

/*
 * check-name: smatch impossible #2
 * check-command: ./smatch -I.. sm_impossible2.c
 *
 * check-output-start
sm_impossible2.c:6 main() warn: always true condition '(x >= 0) => (0-u32max >= 0)'
sm_impossible2.c:7 main() no states found for 'smatch_impossible_return'
sm_impossible2.c:7 main() smatch_impossible_return: no states
sm_impossible2.c:9 main() [smatch_impossible_return] line=6 impossible (nil) = 'impossible'
 * check-output-end
 */
