static int foo(int a)
{
	a |=\1;

	return a;
}
/*
 * check-name: bad assignment
 *
 * check-command: sparse $file
 * check-exit-value: 1
 *
 * check-output-start
bad-assignment.c:3:6: error: Expected ; at end of statement
bad-assignment.c:3:6: error: got \
 * check-output-end
 *
 * check-known-to-fail
 */
