extern const int *p;
extern volatile void *q;
extern volatile int *r;
static void f(void)
{
	q = 1 ? p : q;	// warn: const volatile void * -> const int *
	r = 1 ? r : q;	// OK: volatile void * -> volatile int *
	r = 1 ? r : p;	// warn: const volatile int * -> volatile int *
}
/*
 * check-name: type of conditional expression
 * check-description: Used to miss qualifier mixing and mishandle void *
 *
 * check-error-start
cond_expr2.c:6:4: warning: incorrect type in assignment (different modifiers)
cond_expr2.c:6:4:    expected void volatile *extern [addressable] [toplevel] q
cond_expr2.c:6:4:    got void const volatile *
cond_expr2.c:8:4: warning: incorrect type in assignment (different modifiers)
cond_expr2.c:8:4:    expected int volatile *extern [addressable] [toplevel] [assigned] r
cond_expr2.c:8:4:    got int const volatile *
 * check-error-end
 */
