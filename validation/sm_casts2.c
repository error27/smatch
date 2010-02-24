#include <stdio.h>

unsigned int frob();

unsigned char *a;
unsigned int *b;
int *c;
char *****d;
int main(void)
{

	if (*a == (unsigned int)-1)
		frob();
	if (*b == (unsigned int)-1)
		frob();
	if (*c == (unsigned int)-1)
		frob();
	if (*d == (unsigned int)-1)
		frob();
	if (*d == -1)
		frob();
	if (*****d == (unsigned int)-1)
		frob();
	return 0;
}
/*
 * check-name: smatch casts pointers
 * check-command: smatch sm_casts2.c
 *
 * check-output-start
sm_casts2.c +12 main(3) warn: 4294967295 is more than 255 (max '*a' can be) so this is always false.
sm_casts2.c +16 main(7) warn: 4294967295 is more than 2147483647 (max '*c' can be) so this is always false.
sm_casts2.c +18 main(9) warn: 4294967295 is more than 2147483647 (max '*d' can be) so this is always false.
sm_casts2.c +22 main(13) warn: 4294967295 is more than 127 (max '*****d' can be) so this is always false.
 * check-output-end
 */
