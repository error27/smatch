static inline int def(void)
{
	return 1;
}

int foo(void)
{
	return def();
}

int bar(void)
{
	return def();
	return 0;
}

/*
 * check-name: inline-return.c
 * check-command: test-linearize -fdump-ir=linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..*\\$1
 * check-output-excludes: ret\\..*\\$0
 */
