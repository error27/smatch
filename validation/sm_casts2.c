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
sm_casts2.c:12 main() error: *a is never equal to max (wrong type 0 - 255).
sm_casts2.c:22 main() error: *****d is never equal to max (wrong type (-128) - 127).
 * check-output-end
 */
