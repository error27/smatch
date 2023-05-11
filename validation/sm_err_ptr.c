#include <stdio.h>

int *add_inode();
#define IS_ERR_VALUE(x) ((x) >= (unsigned long)-4095)
static int IS_ERR(const void *ptr)
{
	return IS_ERR_VALUE((unsigned long)ptr);
}

int main(void)
{
	int *p;

	p = add_inode();
	if (!IS_ERR(p)) {
		*p = 1;
	}
	*p = 1;

	return 0;
}
/*
 * check-name: dereferencing ERR_PTR bugs
 * check-command: smatch -p=kernel sm_err_ptr.c
 *
 * check-output-start
sm_err_ptr.c:18 main() error: 'p' dereferencing possible ERR_PTR()
 * check-output-end
 */
