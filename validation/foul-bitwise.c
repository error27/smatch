typedef unsigned short __attribute__((bitwise))__le16;
__le16 foo(__le16 a)
{
	return a |= ~a;
}

int baz(__le16 a)
{
	return ~a == ~a;
}

int barf(__le16 a)
{
	return a == (a & ~a);
}

__le16 bar(__le16 a)
{
	return -a;
}
