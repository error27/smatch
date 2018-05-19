struct {
	int a[2];
} s;

int sarray(void)
{
	s.a[1] = 1;
	return s.a[1];
}

/*
 * check-name: init global array
 * check-command: test-linearize -Wno-decl -fdump-ir=mem2reg $file
 * check-output-ignore
 * check-output-excludes: load\\.
 * check-output-pattern(1): store\\.
 */
