static void foo(void)
{
	const char *name = ({ __func__; });
}
/*
 * check-name: __func__'s scope
 * check-command: sparse -Wall $file
 */
