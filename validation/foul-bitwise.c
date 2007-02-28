typedef unsigned short __attribute__((bitwise))__le16;
static __le16 foo(__le16 a)
{
	return a |= ~a;
}

static int baz(__le16 a)
{
	return ~a == ~a;
}

static int barf(__le16 a)
{
	return a == (a & ~a);
}

static __le16 bar(__le16 a)
{
	return -a;
}
