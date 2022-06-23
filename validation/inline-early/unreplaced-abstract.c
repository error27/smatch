static inline void f0(void) { }
static inline long f1(long a) { return a + 1;}

_Static_assert([typeof(f0)] != [typeof(f1)]);


static inline void g0(void) { }
static inline long g1(long a) { return a + 1;}

extern long goo(long a);
long goo(long a)
{
	g0();
	return g1(a);
}

_Static_assert([typeof(g0)] != [typeof(g1)]);

extern long moo(long a);
long moo(long a)
{
	typeof(f1) *f = g1;
	return f(a);
}

/*
 * check-name: unreplaced-abstract
 */
