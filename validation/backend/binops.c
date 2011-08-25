static int add(int x, int y)
{
	return x + y;
}

static unsigned int uadd(unsigned int x, unsigned int y)
{
	return x + y;
}

static int sub(int x, int y)
{
	return x - y;
}

static unsigned int usub(unsigned int x, unsigned int y)
{
	return x - y;
}

static int mul(int x, int y)
{
	return x * y;
}

static unsigned int umul(unsigned int x, unsigned int y)
{
	return x * y;
}

static int div(int x, int y)
{
	return x / y;
}

static unsigned int udiv(unsigned int x, unsigned int y)
{
	return x / y;
}

static int mod(int x, int y)
{
	return x % y;
}

static unsigned int umod(unsigned int x, unsigned int y)
{
	return x % y;
}

static int shl(int x, int y)
{
	return x << y;
}

static unsigned int ushl(unsigned int x, unsigned int y)
{
	return x << y;
}

static int shr(int x, int y)
{
	return x >> y;
}

static unsigned int ushr(unsigned int x, unsigned int y)
{
	return x >> y;
}

static int and(int x, int y)
{
	return x & y;
}

static unsigned int uand(unsigned int x, unsigned int y)
{
	return x & y;
}

static int or(int x, int y)
{
	return x | y;
}

static unsigned int uor(unsigned int x, unsigned int y)
{
	return x | y;
}

static int xor(int x, int y)
{
	return x ^ y;
}

static unsigned int uxor(unsigned int x, unsigned int y)
{
	return x ^ y;
}

#if 0
static int and_bool(int x, int y)
{
	return x && y;
}

static unsigned int uand_bool(unsigned int x, unsigned int y)
{
	return x && y;
}

static int or_bool(int x, int y)
{
	return x || y;
}

static unsigned int uor_bool(unsigned int x, unsigned int y)
{
	return x || y;
}
#endif

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
 * check-name: binary op code generation
 * check-command: ./sparsec -c $file -o tmp.o
 */
