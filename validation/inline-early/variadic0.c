static inline void fun(const char *fmt, ...)
{
}

void main(void)
{
	fun("abc", 0);			// will be a SYM_BASETYPE
	fun("ijk", (const int)1);	// will be a SYM_NODE
}

/*
 * check-name: variadic0
 */
