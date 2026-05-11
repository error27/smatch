#include <stdio.h>
#include "check_debug.h"

void *frob(void);

void main(int *p)
{
	do {
		printf("%d\n", *p);
		if ((p = frob()))
			printf("%d\n", *p);
	} while ((p = frob()));
}

/*
 * check-name: smatch: two passes #3
 * check-command: smatch -I.. sm_two_passes3.c
 */
