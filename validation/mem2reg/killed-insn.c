static int g;
static void foo(void)
{
	int a[2] = { };
	a;
	a[1] = g;
}

/*
 * check-name: killed-insn
 * check-command: test-linearize -fdump-ir=mem2reg $file
 *
 * check-output-ignore
 * check-output-excludes: store\\.
 */
