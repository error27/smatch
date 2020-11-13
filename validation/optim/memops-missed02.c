void foo(int a[])
{
	int i, val;
	for (;; i++)
		val = a[i] ? a[i] : val;
}

/*
 * check-name: memops-missed02
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-pattern(1): load\\.
 */
