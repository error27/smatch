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
sm_casts2.c:12 main(3) error: *a is never equal to 4294967295 (wrong type 0 - 255).
sm_casts2.c:22 main(13) error: *****d is never equal to 4294967295 (wrong type -128 - 127).
 * check-output-end
 */
