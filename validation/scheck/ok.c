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

/*
 * check-name: scheck-ok
 * check-command: scheck $file
 */
