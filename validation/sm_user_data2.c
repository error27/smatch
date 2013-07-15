#include "check_debug.h"

int copy_from_user(void *dest, void *src, int size){}

struct my_struct {
	int x, y;
};

void *pointer;
struct my_struct *dest;

struct my_struct *returns_copy(void)
{
	copy_from_user(dest, pointer, sizeof(*dest));
	return dest;
}

struct my_struct *a;
void test(void)
{
	a = returns_copy();
	__smatch_state("check_user_data", "a->x");
}

/*
 * check-name: smatch user data #2
 * check-command: smatch -p=kernel -I.. sm_user_data2.c
 *
 * check-output-start
sm_user_data2.c:22 test() 'a->x' = 'user_data_set'
 * check-output-end
 */
