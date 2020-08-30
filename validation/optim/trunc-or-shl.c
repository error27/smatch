// because of the cast to char, the fist arg should be eliminated
// and the whole reduced to TRUNC(%arg2, 8)

char foo(int a, int b)
{
	return (a << 8) | b;
}

/*
 * check-name: trunc-or-shl
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-excludes: %arg1
 * check-output-contains: trunc\\..*%arg2
 */
