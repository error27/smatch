#include "check_debug.h"

void initialize(void *p);

int main(int x)
{
	unsigned int aaa[10];
	int y;

	initialize(&aaa);
	initialize(&y);

	if (aaa[5] > 3)
		return 0;
	aaa[0] = 42;
	__smatch_implied(aaa[0]);
	__smatch_implied(aaa[5]);
	aaa[y] = 10;
	__smatch_implied(aaa[5]);
	if (aaa[y] > 4)
		return 0;
	__smatch_implied(aaa[y]);
	y = 3;
	__smatch_implied(aaa[y]);

	return 0;
}

/*
 * check-name: smatch chunk #2
 * check-command: smatch -I.. sm_chunk2.c
 *
 * check-output-start
sm_chunk2.c:16 main() implied: aaa[0] = '42'
sm_chunk2.c:17 main() implied: aaa[5] = '0-3'
sm_chunk2.c:19 main() implied: aaa[5] = '0-u32max'
sm_chunk2.c:22 main() implied: aaa[y] = '0-4'
sm_chunk2.c:24 main() implied: aaa[y] = '0-u32max'
 * check-output-end
 */
