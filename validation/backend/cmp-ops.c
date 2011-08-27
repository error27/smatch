static int sete(int x, int y)
{
	return x == y;
}

static int setne(int x, int y)
{
	return x != y;
}

static int setl(int x, int y)
{
	return x < y;
}

static int setg(int x, int y)
{
	return x > y;
}

/*
 * check-name: Comparison operator code generation
 * check-command: ./sparsec -c $file -o tmp.o
 */
