int test(int x, int y)
{
	if (x = 1)
		return 1;
	if (x = y)
		return 2;
	if (x == 1)
		return 3;
	return 0;
}

/*
 * check-name: smatch: check assignment versus comparison
 * check-command: smatch --no-data sm_check_assign_vs_compare.c
 *
 * check-output-start
sm_check_assign_vs_compare.c:3 test() warn: was '== 1' instead of '='
 * check-output-end
 */
