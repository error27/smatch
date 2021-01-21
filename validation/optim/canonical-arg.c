int def(void);

int canon_arg_arg(int a, int b)
{
	return (a + b) == (b + a);
}

int canon_arg_reg(int a)
{
	int b = def();
	return (a + b) == (b + a);
}

/*
 * check-name: canonical-arg
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
