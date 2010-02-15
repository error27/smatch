#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

int a[] = {1, 2 , 3, 4};

int main(void)
{
	short *s = a;
	char buf[4];
	int i;

	printf("%d\n", s[1]);
	printf("%d\n", s[2]);
	printf("%d\n", s[3]);
	printf("%d\n", s[4]);
	printf("%d\n", s[5]);
	printf("%d\n", s[6]);
	printf("%d\n", s[7]);
	printf("%d\n", s[8]);
	strcpy(buf, "1234");

	return 0;
}
/*
 * check-name: smatch overflow check #4
 * check-command: smatch sm_array_overflow4.c
 *
 * check-output-start
sm_array_overflow4.c +21 main(13) error: buffer overflow 's' 8 <= 8
sm_array_overflow4.c +22 main(14) error: strcpy() "1234" too large for buf (5 vs 4)
 * check-output-end
 */
