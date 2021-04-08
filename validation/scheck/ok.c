static void ok(int x)
{
	__assert((~x) == (~0 - x));	// true but not simplified yet
}

static void always(int x)
{
	__assert((x - x) == 0);		// true and simplified
}

/*
 * check-name: scheck-ok
 * check-command: scheck $file
 */
