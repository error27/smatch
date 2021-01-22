_Bool sel_lts(int a, int b, int x, int y)
{
	return ((a < b) ? x : y) == ((a >= b) ? y : x);
}
_Bool sel_les(int a, int b, int x, int y)
{
	return ((a <= b) ? x : y) == ((a > b) ? y : x);
}

_Bool sel_ltu(unsigned int a, unsigned int b, int x, int y)
{
	return ((a < b) ? x : y) == ((a >= b) ? y : x);
}
_Bool sel_leu(unsigned int a, unsigned int b, int x, int y)
{
	return ((a <= b) ? x : y) == ((a > b) ? y : x);
}

/*
 * check-name: canonical-cmps-sel
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
