int test_array(int i)
{
	static const int a[3] = { 1, 2, 3, };

	return a[1];
}

/*
 * check-name: constant-init-array
 * check-command: test-linearize -Wno-decl -fdump-ir $file
 *
 * check-output-ignore
 * check-output-excludes: phisrc\\..*return.*\\$2
 * check-output-contains: load\\.
 */
