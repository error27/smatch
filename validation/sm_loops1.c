#include "check_debug.h"

void frob();

void func(int *x)
{
	int a,b,c;

	for (a = 0; a < 10; a++)
		__smatch_value("a");
	__smatch_value("a");
	for (; a != 10; a++)
		__smatch_value("a");
	__smatch_value("a");
	for (a = 0; a != 10; a++)
		__smatch_value("a");
	__smatch_value("a");
	for (a = 0; a <= 10; a++)
		__smatch_value("a");
	__smatch_value("a");
	return;
}
/*
 * check-name: smatch loops #1
 * check-command: smatch -I.. sm_loops1.c
 *
 * check-output-start
sm_loops1.c:10 func(5) a = 0-9
sm_loops1.c:11 func(6) a = 10
sm_loops1.c:13 func(8) a = 
sm_loops1.c:14 func(9) a = 10
sm_loops1.c:16 func(11) a = 0-9
sm_loops1.c:17 func(12) a = 10
sm_loops1.c:19 func(14) a = 0-10
sm_loops1.c:20 func(15) a = 11
 * check-output-end
 */
