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

void *frob(void *p)
{
	if (!p || IS_ERR(p))
		return ERR_PTR(-12);
	return p;
}

int main(int *p)
{
	int ret, ret2;

	ret = PTR_ERR(p);
	if (IS_ERR(p)) {
		__smatch_implied(p);
		ret = PTR_ERR(p);
		__smatch_implied(ret);
	}

	__smatch_implied(ERR_PTR(-12));

	p = frob(p);
	__smatch_implied(p);

	p = ERR_PTR(-12);
	__smatch_implied(p);

	return 0;
}

/*
 * check-name: smatch: error pointer #3
 * check-command: smatch -p=kernel -I.. sm_err_ptr3.c
 *
 * check-output-start
sm_err_ptr3.c:32 main() implied: p = '(-4095)-(-1)'
sm_err_ptr3.c:34 main() implied: ret = '(-4095)-(-1)'
sm_err_ptr3.c:37 main() implied: ERR_PTR(-12) = '(-12)'
sm_err_ptr3.c:40 main() implied: p = '1-ptr_max,(-12)'
sm_err_ptr3.c:43 main() implied: p = '(-12)'
 * check-output-end
 */
