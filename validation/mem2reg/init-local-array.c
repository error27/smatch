int array(void)
{
	int a[2];

	a[1] = 1;
	return a[1];
}

int sarray(void)
{
	struct {
		int a[2];
	} s;

	s.a[1] = 1;
	return s.a[1];
}

/*
 * check-name: init local array
 * check-command: test-linearize -Wno-decl -fdump-ir=mem2reg $file
 * check-output-ignore
 * check-output-excludes: load\\.
 * check-output-excludes: store\\.
 */
