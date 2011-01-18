#include "check_debug.h"

void strncpy(char *to, char *from, int size);

void func (char *a, char *b)
{
	strncpy(a, b, 5);
	a[5] = '\0';
}
/*
 * check-name: smatch strncpy() overflow
 * check-command: smatch --spammy -I.. sm_overflow2.c
 *
 * check-output-start
sm_overflow2.c +8 func(3) error: buffer overflow 'a' 5 <= 5
 * check-output-end
 */
