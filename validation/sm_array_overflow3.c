#include <stdio.h>

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

int a[] = {1, 2, 3, 4};

int main(void)
{
	int *p;

	for (p = a; p < &a[ARRAY_SIZE(a)]; p++)
		printf("%d\n", *p);
	p = &a[4];
	return 0;
}
/*
 * check-name: smatch array check #3
 * check-command: smatch sm_array_overflow3.c
 *
 * check-output-start
sm_array_overflow3.c +13 main(6) warn: buffer overflow 'a' 4 <= 4
 * check-output-end
 */
