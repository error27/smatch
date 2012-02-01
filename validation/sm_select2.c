#include "check_debug.h"

int check();

int x,y,z;
int func(void)
{
	int *sd = &x;

	while (!(! sd ? -19 :
			(*sd ?
				check() : -515)))
		__smatch_value("sd");
}
/*
 * check-name: Ternary Conditions #2
 * check-command: smatch -I.. sm_select2.c
 *
 * check-output-start
sm_select2.c:13 func(7) sd = min-(-1),1-max
 * check-output-end
 */
