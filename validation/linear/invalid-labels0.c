static void foo(void)
{
	goto return;
}

void bar(void)
{
	goto neverland;
}

/*
 * check-name: invalid-labels0
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-excludes: END
 * check-error-ignore
 */
