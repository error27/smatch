static void test_volatile(void)
{
	volatile int x = 0;
	int *pp;

	typeof(++x)		v1; pp = &v1;	// KO
	typeof(x++)		v2; pp = &v2;	// KO
}

/*
 * check-name: unqual-postop
 * check-command: sparse -Wno-declaration-after-statement $file
 * check-known-to-fail
 *
 * check-error-start
eval/unqual-postop.c:6:40: warning: incorrect type in assignment (different modifiers)
eval/unqual-postop.c:6:40:    expected int *pp
eval/unqual-postop.c:6:40:    got int volatile *
eval/unqual-postop.c:7:40: warning: incorrect type in assignment (different modifiers)
eval/unqual-postop.c:7:40:    expected int *pp
eval/unqual-postop.c:7:40:    got int volatile *
 * check-error-end
 */
