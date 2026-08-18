#include "check_debug.h"

void first(int ignored, int value);
void second(int value);

void func(int value)
{
	first(0, value);
	second(value);
	__smatch_passed_to(value);
}

/*
 * check-name: smatch passed to
 * check-command: smatch -I.. sm_passed_to.c
 *
 * check-output-start
sm_passed_to.c:10 func() passed to first $1
sm_passed_to.c:10 func() passed to second $0
 * check-output-end
 */
