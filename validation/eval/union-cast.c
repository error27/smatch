union u {
	int	i;
	char	x[8];
};

static union u foo(int a)
{
	return (union u)a;
}

static union u bar(long a)
{
	return (union u)a;
}

/*
 * check-name: union-cast
 * check-command: sparse -Wunion-cast $file
 *
 * check-error-start
eval/union-cast.c:8:17: warning: cast to union type
eval/union-cast.c:13:17: warning: cast to non-scalar
 * check-error-end
 */
