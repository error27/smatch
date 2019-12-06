int test_array(int i)
{
	static const int a[3] = { [0] = 1, [2] = 3, };

	return a[1];
}

/*
 * check-name: default-init-array
 * check-command: test-linearize -Wno-decl -fdump-ir $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-contains: phisrc\\..*return.*\\$0
 * check-output-excludes: load\\.
 */
