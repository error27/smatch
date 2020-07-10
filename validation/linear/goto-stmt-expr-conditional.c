int t(void)
{
	goto inside;
	return 1 ? 2 : ({
inside:
			return 3;
			4;
		    });
}

void f(int x, int y)
{
	1 ? x : ({
a:
		 y;
	});
	goto a;
}

/*
 * check-name: goto-stmt-expr-conditional
 * check-command: test-linearize -Wno-decl $file
 *
 * check-error-ignore
 * check-output-ignore
 * check-output-excludes: END
 */
