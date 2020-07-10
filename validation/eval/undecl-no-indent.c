inline void fun(void)
{
	undecl();
}

void foo(void);
void foo(void)
{
	fun();
	fun();
}

/*
 * check-name: undecl-no-indent
 *
 * check-error-start
eval/undecl-no-indent.c:3:9: error: undefined identifier 'undecl'
 * check-error-end
 */
