#include <stdio.h>
#include <stdlib.h>

struct foo {
	int a;
};

int main (void)
{
	int *a = malloc(sizeof(*a));

	*(*(&a)) = 1;
	printf("%d\n", *a);
	**&a = 2;
	printf("%d\n", *a);

	return 0;
}

/*
 * smatch_helper.c does a crap job creating variable names...  It needs
 * to be fixed so that it could understand the above code.
 */
