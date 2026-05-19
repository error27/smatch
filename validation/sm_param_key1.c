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

int *frob(void);

int func(int *p)
{
	int *ret;

	if (!p) {
		p = frob();
		if (IS_ERR(p)) {
			ret = PTR_ERR(p);
			__smatch_return_str(ret);
			return PTR_ERR(p);
		}
	}
	return 0;
}

/*
 * check-name: smatch: param key #1
 * check-command: smatch -I.. sm_param_key1.c
 *
 * check-output-start
sm_param_key1.c:29 func() ret_str='(-4095)-(-1)'
 * check-output-end
 */
