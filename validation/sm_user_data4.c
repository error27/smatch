#include "check_debug.h"

int copy_from_user(void *dest, void *src, int size);

struct ear {
	int x, y;
};

void *src;
int returns_user_data(void)
{
	int x;

	copy_from_user(&x, src, sizeof(int));
	return x;
}

struct ear *dest;
struct ear *returns_user_member(void)
{
	copy_from_user(&dest->x, src, sizeof(int));
	return dest;
}
void test(void)
{
	struct ear *p;
	int x;

	x = returns_user_data();
	__smatch_state("check_user_data", "x");
	p = returns_user_member();
	__smatch_state("check_user_data", "p");
	__smatch_state("check_user_data", "p->x");
}

/*
 * check-name: smatch user data #4
 * check-command: smatch -p=kernel -I.. sm_user_data4.c
 *
 * check-output-start
sm_user_data4.c:30 test() 'x' = 'user_data_set'
sm_user_data4.c:32 test() check_user_data 'p' not found
sm_user_data4.c:33 test() 'p->x' = 'user_data_set'
 * check-output-end
 */
