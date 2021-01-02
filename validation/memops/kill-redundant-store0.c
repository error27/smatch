void foo(int *ptr)
{
	int i = *ptr;
	*ptr = i;
}

/*
 * check-name: kill-redundant-store0
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-excludes: store
 */
