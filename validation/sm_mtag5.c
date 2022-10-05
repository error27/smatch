#include <stdio.h>
#include "check_debug.h"

int x = 42;

struct foo {
	int a, b, c;
};
struct foo aaa = {
	.a = 1, .b = 2, .c = 3,
};

int array[10];

int main(void)
{
	__smatch_implied(&x);
	__smatch_implied(&aaa);
	__smatch_implied(&aaa.b);
	__smatch_implied(array);
	__smatch_implied(&array[1]);

	return 0;
}

/*
 * check-name: smatch mtag #5
 * check-command: smatch -I.. sm_mtag5.c
 *
 * check-output-start
sm_mtag5.c:17 main() implied: &x = '7475740836921503744'
sm_mtag5.c:18 main() implied: &aaa = '6045882893841539072'
sm_mtag5.c:19 main() implied: &aaa.b = '6045882893841539076'
sm_mtag5.c:20 main() implied: array = '685747191403540480'
sm_mtag5.c:21 main() implied: &array[1] = '685747191403540484'
 * check-output-end
 */
