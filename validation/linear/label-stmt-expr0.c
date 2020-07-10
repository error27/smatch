int foo(void);
int foo(void)
{
	int r;

	r = ({ goto label; label: 1; });
	return r;
}

/*
 * check-name: label-stmt-expr0
 * check-command: test-linearize $file
 * check-output-ignore
 *
 * check-output-excludes: ret\\.32\$
 * check-output-contains: ret\\.32 *\\$1
 */
