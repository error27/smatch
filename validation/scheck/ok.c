static void ok(int x)
{
	__assert((~x) == (~0 - x));	// true but not simplified yet
	__assert_eq(~x, ~0 - x);
	__assert_const(x & 0, 0);
}

static void always(int x)
{
	__assert((x - x) == 0);		// true and simplified
}

static void assumed(int x, int a, int b)
{
	__assume((a & ~b) == 0);
	__assert_eq((x ^ a) | b, x | b);
}

/*
 * check-name: scheck-ok
 * check-command: scheck $file
 */
