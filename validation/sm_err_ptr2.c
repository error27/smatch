#include "check_debug.h"

#define IS_ERR_VALUE(x) ((x) >= (unsigned long)-4095)
static int IS_ERR(const void *ptr)
{
	return IS_ERR_VALUE(ptr);
}

static long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}

static void *ERR_PTR(int err)
{
	return (void *)(unsigned long)err;
}

int main(int *p)
{
	p = ERR_PTR(-12);
	__smatch_implied(p);
	return 0;
}

/*
 * check-name: smatch: error pointer #2
 * check-command: smatch -I.. sm_err_ptr2.c
 *
 * check-output-start
sm_err_ptr2.c:22 main() implied: p = '18446744073709551604'
 * check-output-end
 */
