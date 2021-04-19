#define N	1024

_Bool check_ok(long i)
{
	return i >= 0 && i < N;
}

/*
 * check-name: range-check1
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: setbe\\..*0x3ff
 * check-output-excludes: set[lga][te]\\.
 * check-output-excludes: set[ab]\\.
 */
