_Bool isdigit(int c)
{
	return __builtin_isdigit(c) == (((unsigned) (c - '0')) <= 9);
}

/*
 * check-name: builtin_isdigit
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
