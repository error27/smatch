union u {
	int	i;
	char	x[8];
};

static union u foo(int i)
{
	return (union u)i;
}

static union u bar(long l)
{
	return (union u)l;
}

/*
 * check-name: union-cast-no
 * check-command: sparse -Wno-union-cast $file
 *
 * check-error-start
eval/union-cast-no.c:13:17: warning: cast to non-scalar
 * check-error-end
 */
