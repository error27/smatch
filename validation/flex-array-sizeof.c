struct s {
	int i;
	long f[];
};

static int foo(struct s *s)
{
	return sizeof(*s);
}

/*
 * check-name: flex-array-sizeof
 * check-command: sparse -Wflexible-array-sizeof $file
 *
 * check-error-start
flex-array-sizeof.c:8:16: warning: using sizeof on a flexible structure
 * check-error-end
 */
