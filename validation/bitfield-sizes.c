struct a {
	int  a:31;
	int  b:32;
	long c:63;
	long d:64;
	int  x:33;		// KO
	long y:65;		// KO
};
static struct a a;

struct b {
	int m1:-1;		// KO
	int x1:2147483648;	// KO
	int :0;
	int a0:0;		// KO
};
static struct b b;

/*
 * check-name: bitfield-sizes
 * check-command: sparse -m64 $file
 *
 * check-error-start
bitfield-sizes.c:12:18: error: invalid bitfield width, -1.
bitfield-sizes.c:13:26: error: invalid bitfield width, 2147483648.
bitfield-sizes.c:15:17: error: invalid named zero-width bitfield `a0'
bitfield-sizes.c:6:15: error: impossible field-width, 33, for this type
bitfield-sizes.c:7:15: error: impossible field-width, 65, for this type
 * check-error-end
 */
