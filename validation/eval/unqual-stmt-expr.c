#define __unqual_typeof(x) typeof(({ x; }))

int *foo(volatile int x);
int *foo(volatile int x)
{
	extern __unqual_typeof(x) y;
	return &y;
}

/*
 * check-name: unqual-stmt-expr
 */
