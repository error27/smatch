static void f(void)
{
	void *param = param;
	__builtin_object_size(param, 0);
}

/*
 * check-name: builtin-objsize-self-init
 * check-timeout:
 * check-error-end
 */
