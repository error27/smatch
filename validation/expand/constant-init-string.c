char foo(void)
{
	static const char s[] = "abc?";
	return s[3];
}

/*
 * check-name: constant-init-nested-array
 * check-command: test-linearize -Wno-decl -fdump-ir $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-contains: phisrc\\..*\\$63
 * check-output-pattern(0,1): load\\.
 */
