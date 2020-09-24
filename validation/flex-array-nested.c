struct f {
	int i;
	long f[];
};

struct s {
	struct f f;
};

union u {
	struct f f;
};

// trigger the examination of the offending types
static int foo(struct s *s, union u *u)
{
	return    __builtin_offsetof(typeof(*s), f)
		+ __builtin_offsetof(typeof(*u), f);
}

/*
 * check-name: flex-array-nested
 * check-command: sparse -Wflexible-array-nested $file
 *
 * check-error-start
flex-array-nested.c:6:8: warning: nested flexible arrays
flex-array-nested.c:10:7: warning: nested flexible arrays
 * check-error-end
 */
