#include "check_debug.h"

int copy_from_user(void *dest, void *src, int size){}

struct my_struct {
	int x, y;
};

struct my_struct *returns_filter(struct my_struct *p)
{
	return p;
}

struct my_struct *src, *a, *b;
void test(void)
{
	copy_from_user(a, src, sizeof(*a));
	b = returns_filter(a);
	__smatch_state("check_user_data", "b->y");
	b = returns_filter(src);
	__smatch_state("check_user_data", "b->y");
	b = returns_filter(a);
	__smatch_state("check_user_data", "b->y");
}

/*
 * check-name: smatch user data #3
 * check-command: smatch -p=kernel -I.. sm_user_data3.c
 *
 * check-output-start
sm_user_data3.c:19 test() 'b->y' = 'user_data_set'
sm_user_data3.c:21 test() 'b->y' = 'capped'
sm_user_data3.c:23 test() 'b->y' = 'user_data_set'
 * check-output-end
 */
