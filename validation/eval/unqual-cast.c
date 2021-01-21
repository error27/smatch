#define cvr const volatile restrict

_Static_assert([typeof((cvr int) 0)] == [int]);
_Static_assert([typeof((cvr int *) 0)] == [cvr int *]);

static int *function(volatile int x)
{
	extern typeof((typeof(x)) (x)) y;
	return &y;
}

/*
 * check-name: unqual-cast
 */
