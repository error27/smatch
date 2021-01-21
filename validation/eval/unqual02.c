static void test_const(volatile int x)
{
	const int x = 0;
	typeof(1?x:x)		i3; i3 = 0;	// should be OK
	typeof(+x)		i4; i4 = 0;	// should be OK
	typeof(-x)		i5; i5 = 0;	// should be OK
	typeof(!x)		i6; i6 = 0;	// should be OK
	typeof(x+x)		i7; i7 = 0;	// should be OK
}

static void test_volatile(void)
{
	volatile int x = 0;
	int *pp;

	typeof(1?x:x)		i3; pp = &i3;	// should be OK
	typeof(+x)		i4; pp = &i4;	// should be OK
	typeof(-x)		i5; pp = &i5;	// should be OK
	typeof(!x)		i6; pp = &i6;	// should be OK
	typeof(x+x)		i7; pp = &i7;	// should be OK
}

/*
 * check-name: unqual02
 * check-command: sparse -Wno-declaration-after-statement $file
 */
