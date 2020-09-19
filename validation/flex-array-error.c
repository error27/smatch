struct s {
	int i;
	long f[];
	int j;
};

union u {
	int i;
	long f[];
};

// trigger the examination of the offending types
static int foo(struct s *s, union u *u)
{
	return    __builtin_offsetof(typeof(*s), i)
		+ __builtin_offsetof(typeof(*u), i);
}

/*
 * check-name: flex-array-error
 *
 * check-error-start
flex-array-error.c:3:14: error: flexible array member 'f' is not last
flex-array-error.c:9:14: error: flexible array member 'f' in a union
 * check-error-end
 */
