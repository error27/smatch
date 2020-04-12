static void foo(void)
{
	goto return;
}

void bar(void)
{
	goto neverland;
}

/*
 * check-name: goto-invalid
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-error-ignore
 * check-output-ignore
 * check-output-excludes: END
 */
