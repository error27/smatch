#define __unqual_typeof(x) typeof(((void)0, (x)))

int *foo(volatile int x);
int *foo(volatile int x)
{
	extern __unqual_typeof(x) y;
	return &y;
}

/*
 * check-name: unqual-comma
 */
