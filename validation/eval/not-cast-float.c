static int foo(void)
{
	int i = 123;
	float x = ~i;
	return (x < 0);
}

/*
 * check-name: eval-bool-zext-neg
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
