// should warn: "warning: Expected : in conditional expression"
// and should not segfault

static int foo(int a)
{
	return a ?? 1 : 0;
}
