static void foo(int x)
{
	__context__(0);		// OK
	__context__(x, 0);	// OK
	__context__ (x, 1);	// OK

	__context__(x);		// KO: no const expr
	__context__(1,x);	// KO: no const expr
}

/*
 * check-name: context-stmt
 * check-command: sparse -Wno-context $file
 *
 * check-error-start
context-stmt.c:7:21: error: bad constant expression
context-stmt.c:8:23: error: bad constant expression
 * check-error-end
 */
