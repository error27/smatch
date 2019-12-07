int foo(void)
{
	int a[2][3] = {{0, 1, 2},{3, 4, 5}};
	return a[1][2];
}

/*
 * check-name: constant-init-nested-array
 * check-command: test-linearize -Wno-decl -fdump-ir $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-contains: phisrc\\..*\\$5
 * check-output-excludes: load\\.
 */
