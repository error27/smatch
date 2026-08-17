int returns_int(void);
void returns_void(void);

void test(void)
{
	returns_int();
	returns_void();
}

/*
 * check-name: smatch: check all function returns
 * check-command: smatch --no-data -p=illumos_user sm_check_all_func_returns.c
 *
 * check-output-start
sm_check_all_func_returns.c:6 test() error: unchecked function return 'returns_int'
 * check-output-end
 */
