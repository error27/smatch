#include "check_debug.h"

#define add(x, y) x + y
#define sub(x, y) x - y

void func(int *p)
{
	int a = 1;
	int b = 2;

	x = 4 * add(2, 3);
	x = 4 + add(2, 3);
	x = 4 * add(2, 3) * 8;
	x = add(2, 3) * 4;
	x = add(2, 3) - 4;
	x = -sub(2, 3);
	x = sub(2, 3)++;
}
/*
 * check-name: Smatch macro precedence bugs
 * check-command: smatch -I.. sm_macros.c
 *
 * check-output-start
sm_macros.c +11 func(5) warn: the 'add' macro might need parens
sm_macros.c +13 func(7) warn: the 'add' macro might need parens
sm_macros.c +13 func(7) warn: the 'add' macro might need parens
sm_macros.c +14 func(8) warn: the 'add' macro might need parens
sm_macros.c +16 func(10) warn: the 'sub' macro might need parens
sm_macros.c +17 func(11) warn: the 'sub' macro might need parens
 * check-output-end
 */
