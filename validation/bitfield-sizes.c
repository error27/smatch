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
bitfield-sizes.c:12:18: error: bitfield 'm1' has invalid width (-1)
bitfield-sizes.c:13:26: error: bitfield 'x1' has invalid width (2147483648)
bitfield-sizes.c:15:17: error: bitfield 'a0' has invalid width (0)
bitfield-sizes.c:6:15: error: bitfield 'x' is wider (33) than its type (int)
bitfield-sizes.c:7:15: error: bitfield 'y' is wider (65) than its type (long)
 * check-error-end
 */
