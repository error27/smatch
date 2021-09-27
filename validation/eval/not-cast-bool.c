static _Bool foo(void)
{
	unsigned char c = 1;
	_Bool b = ~c;
	return b;
}

/*
 * check-name: not-cast-bool
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
