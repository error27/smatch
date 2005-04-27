/*
 *  Bug in original tree: (real_v ? : x) had been treated as equivalent of
 *  (real_v == 0 ? real_v == 0 : x), which gives the wrong type (and no
 *  warning from the testcase below).
 */
int x;
double y;
int a(void)
{
	return ~(y ? : x);	/* should warn */
}
