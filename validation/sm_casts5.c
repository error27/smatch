#include <stdio.h>
#include <string.h>
#include "check_debug.h"

long long l;
long long ul;
int i;
int ui;
signed char c;
char uc;

int main(void)
{
	int idx;

	if (c < -2)
		return 1;
	if (uc < (unsigned int)-2)
		return 1;
	if (i < -2)
		return 1;
	if (ui < (unsigned int)-2)
		return 1;
	if (l < -2)
		return 1;
	if (ul < (unsigned int)-2)
		return 1;

	__smatch_implied(l);
	__smatch_implied(ul);
	__smatch_implied(i);
	__smatch_implied(ui);
	__smatch_implied(c);
	__smatch_implied(uc);

	return 0;
}


/*
 * check-name: smatch: casts #5
 * check-command: smatch -I.. sm_casts5.c
 *
 * check-output-start
sm_casts5.c:29 main() implied: l = '(-2)-s64max'
sm_casts5.c:30 main() implied: ul = '4294967294-s64max'
sm_casts5.c:31 main() implied: i = '(-2)-s32max'
sm_casts5.c:32 main() implied: ui = '(-2)-(-1)'
sm_casts5.c:33 main() implied: c = '(-2)-127'
sm_casts5.c:34 main() implied: uc = '(-2)-(-1)'
 * check-output-end
 */
