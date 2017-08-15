typedef unsigned int uint;
typedef unsigned long ulong;

static int * int_2_iptr(int a) { return (int *)a; }
static int * uint_2_iptr(uint a) { return (int *)a; }

static void * int_2_vptr(int a) { return (void *)a; }
static void * uint_2_vptr(uint a) { return (void *)a; }

/*
 * check-name: cast-weirds
 * check-command: test-linearize -m64 $file
 *
 * check-error-start
cast-weirds.c:4:42: warning: non size-preserving integer to pointer cast
cast-weirds.c:5:44: warning: non size-preserving integer to pointer cast
 * check-error-end
 *
 * check-output-start
int_2_iptr:
.L0:
	<entry-point>
	sext.64     %r2 <- (32) %arg1
	utptr.64    %r3 <- (64) %r2
	ret.64      %r3


uint_2_iptr:
.L2:
	<entry-point>
	zext.64     %r6 <- (32) %arg1
	utptr.64    %r7 <- (64) %r6
	ret.64      %r7


int_2_vptr:
.L4:
	<entry-point>
	sext.64     %r10 <- (32) %arg1
	ret.64      %r10


uint_2_vptr:
.L6:
	<entry-point>
	zext.64     %r13 <- (32) %arg1
	ret.64      %r13


 * check-output-end
 */
