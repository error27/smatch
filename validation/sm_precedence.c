unsigned int x, y;

void func (void)
{
	if (x & y == 0)
		frob();
	if (x + y == 0)
		frob();
	if (x | y == 0)
		frob();
	return;
}
/*
 * check-name: Smatch precedence check
 * check-command: smatch sm_precedence.c
 *
 * check-output-start
sm_precedence.c +5 func(2) warning: do you want parens here?
sm_precedence.c +9 func(6) warning: do you want parens here?
 * check-output-end
 */
