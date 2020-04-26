int foo(int p)
{
	goto inside;
	if (0 && ({
inside:
		return 1;
		2;
		}))
		return 3;
	return 4;
}

int bar(int p)
{
	if (0 && ({
inside:
		return 1;
		2;
		}))
		return 3;
	goto inside;
}

/*
 * check-name: goto-stmt-expr-short-circuit
 * check-command: test-linearize -Wno-decl $file
 *
 * check-error-ignore
 * check-output-ignore
 * check-output-excludes: END
 */
