typedef unsigned int uint;
typedef unsigned long ulong;

static int uint_2_int(uint a) { return (int)a; }
static int long_2_int(long a) { return (int)a; }
static int ulong_2_int(ulong a) { return (int)a; }
static int vptr_2_int(void *a) { return (int)a; }
static int iptr_2_int(int *a) { return (int)a; }
static int float_2_int(float a) { return (int)a; }
static int double_2_int(double a) { return (int)a; }
static uint int_2_uint(int a) { return (uint)a; }
static uint long_2_uint(long a) { return (uint)a; }
static uint ulong_2_uint(ulong a) { return (uint)a; }
static uint vptr_2_uint(void *a) { return (uint)a; }
static uint iptr_2_uint(int *a) { return (uint)a; }
static uint float_2_uint(float a) { return (uint)a; }
static uint double_2_uint(double a) { return (uint)a; }
static long int_2_long(int a) { return (long)a; }
static long uint_2_long(uint a) { return (long)a; }
static long ulong_2_long(ulong a) { return (long)a; }
static long vptr_2_long(void *a) { return (long)a; }
static long iptr_2_long(int *a) { return (long)a; }
static long float_2_long(float a) { return (long)a; }
static long double_2_long(double a) { return (long)a; }
static ulong int_2_ulong(int a) { return (ulong)a; }
static ulong uint_2_ulong(uint a) { return (ulong)a; }
static ulong long_2_ulong(long a) { return (ulong)a; }
static ulong vptr_2_ulong(void *a) { return (ulong)a; }
static ulong iptr_2_ulong(int *a) { return (ulong)a; }
static ulong float_2_ulong(float a) { return (ulong)a; }
static ulong double_2_ulong(double a) { return (ulong)a; }
static void * int_2_vptr(int a) { return (void *)a; }
static void * uint_2_vptr(uint a) { return (void *)a; }
static void * long_2_vptr(long a) { return (void *)a; }
static void * ulong_2_vptr(ulong a) { return (void *)a; }
static void * iptr_2_vptr(int *a) { return (void *)a; }
static int * int_2_iptr(int a) { return (int *)a; }
static int * uint_2_iptr(uint a) { return (int *)a; }
static int * long_2_iptr(long a) { return (int *)a; }
static int * ulong_2_iptr(ulong a) { return (int *)a; }
static int * vptr_2_iptr(void *a) { return (int *)a; }
static float int_2_float(int a) { return (float)a; }
static float uint_2_float(uint a) { return (float)a; }
static float long_2_float(long a) { return (float)a; }
static float ulong_2_float(ulong a) { return (float)a; }
static float double_2_float(double a) { return (float)a; }
static double int_2_double(int a) { return (double)a; }
static double uint_2_double(uint a) { return (double)a; }
static double long_2_double(long a) { return (double)a; }
static double ulong_2_double(ulong a) { return (double)a; }
static double float_2_double(float a) { return (double)a; }

static float float_2_float(float a) { return a; }
static double double_2_double(double a) { return a; }

/*
 * check-name: cast-kinds
 * check-command: test-linearize -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -m64 $file
 *
 * check-output-start
uint_2_int:
.L0:
	<entry-point>
	ret.32      %arg1


long_2_int:
.L2:
	<entry-point>
	scast.32    %r5 <- (64) %arg1
	ret.32      %r5


ulong_2_int:
.L4:
	<entry-point>
	cast.32     %r8 <- (64) %arg1
	ret.32      %r8


vptr_2_int:
.L6:
	<entry-point>
	cast.32     %r11 <- (64) %arg1
	ret.32      %r11


iptr_2_int:
.L8:
	<entry-point>
	ptrtu.64    %r14 <- (64) %arg1
	cast.32     %r15 <- (64) %r14
	ret.32      %r15


float_2_int:
.L10:
	<entry-point>
	fcvts.32    %r18 <- (32) %arg1
	ret.32      %r18


double_2_int:
.L12:
	<entry-point>
	fcvts.32    %r21 <- (64) %arg1
	ret.32      %r21


int_2_uint:
.L14:
	<entry-point>
	ret.32      %arg1


long_2_uint:
.L16:
	<entry-point>
	scast.32    %r27 <- (64) %arg1
	ret.32      %r27


ulong_2_uint:
.L18:
	<entry-point>
	cast.32     %r30 <- (64) %arg1
	ret.32      %r30


vptr_2_uint:
.L20:
	<entry-point>
	cast.32     %r33 <- (64) %arg1
	ret.32      %r33


iptr_2_uint:
.L22:
	<entry-point>
	ptrtu.64    %r36 <- (64) %arg1
	cast.32     %r37 <- (64) %r36
	ret.32      %r37


float_2_uint:
.L24:
	<entry-point>
	fcvtu.32    %r40 <- (32) %arg1
	ret.32      %r40


double_2_uint:
.L26:
	<entry-point>
	fcvtu.32    %r43 <- (64) %arg1
	ret.32      %r43


int_2_long:
.L28:
	<entry-point>
	scast.64    %r46 <- (32) %arg1
	ret.64      %r46


uint_2_long:
.L30:
	<entry-point>
	cast.64     %r49 <- (32) %arg1
	ret.64      %r49


ulong_2_long:
.L32:
	<entry-point>
	ret.64      %arg1


vptr_2_long:
.L34:
	<entry-point>
	cast.64     %r55 <- (64) %arg1
	ret.64      %r55


iptr_2_long:
.L36:
	<entry-point>
	ptrtu.64    %r58 <- (64) %arg1
	ret.64      %r58


float_2_long:
.L38:
	<entry-point>
	fcvts.64    %r61 <- (32) %arg1
	ret.64      %r61


double_2_long:
.L40:
	<entry-point>
	fcvts.64    %r64 <- (64) %arg1
	ret.64      %r64


int_2_ulong:
.L42:
	<entry-point>
	scast.64    %r67 <- (32) %arg1
	ret.64      %r67


uint_2_ulong:
.L44:
	<entry-point>
	cast.64     %r70 <- (32) %arg1
	ret.64      %r70


long_2_ulong:
.L46:
	<entry-point>
	ret.64      %arg1


vptr_2_ulong:
.L48:
	<entry-point>
	cast.64     %r76 <- (64) %arg1
	ret.64      %r76


iptr_2_ulong:
.L50:
	<entry-point>
	ptrtu.64    %r79 <- (64) %arg1
	ret.64      %r79


float_2_ulong:
.L52:
	<entry-point>
	fcvtu.64    %r82 <- (32) %arg1
	ret.64      %r82


double_2_ulong:
.L54:
	<entry-point>
	fcvtu.64    %r85 <- (64) %arg1
	ret.64      %r85


int_2_vptr:
.L56:
	<entry-point>
	scast.64    %r88 <- (32) %arg1
	ret.64      %r88


uint_2_vptr:
.L58:
	<entry-point>
	cast.64     %r91 <- (32) %arg1
	ret.64      %r91


long_2_vptr:
.L60:
	<entry-point>
	scast.64    %r94 <- (64) %arg1
	ret.64      %r94


ulong_2_vptr:
.L62:
	<entry-point>
	cast.64     %r97 <- (64) %arg1
	ret.64      %r97


iptr_2_vptr:
.L64:
	<entry-point>
	cast.64     %r100 <- (64) %arg1
	ret.64      %r100


int_2_iptr:
.L66:
	<entry-point>
	scast.64    %r103 <- (32) %arg1
	utptr.64    %r104 <- (64) %r103
	ret.64      %r104


uint_2_iptr:
.L68:
	<entry-point>
	cast.64     %r107 <- (32) %arg1
	utptr.64    %r108 <- (64) %r107
	ret.64      %r108


long_2_iptr:
.L70:
	<entry-point>
	utptr.64    %r111 <- (64) %arg1
	ret.64      %r111


ulong_2_iptr:
.L72:
	<entry-point>
	utptr.64    %r114 <- (64) %arg1
	ret.64      %r114


vptr_2_iptr:
.L74:
	<entry-point>
	ptrcast.64  %r117 <- (64) %arg1
	ret.64      %r117


int_2_float:
.L76:
	<entry-point>
	scvtf.32    %r120 <- (32) %arg1
	ret.32      %r120


uint_2_float:
.L78:
	<entry-point>
	ucvtf.32    %r123 <- (32) %arg1
	ret.32      %r123


long_2_float:
.L80:
	<entry-point>
	scvtf.32    %r126 <- (64) %arg1
	ret.32      %r126


ulong_2_float:
.L82:
	<entry-point>
	ucvtf.32    %r129 <- (64) %arg1
	ret.32      %r129


double_2_float:
.L84:
	<entry-point>
	fcvtf.32    %r132 <- (64) %arg1
	ret.32      %r132


int_2_double:
.L86:
	<entry-point>
	scvtf.64    %r135 <- (32) %arg1
	ret.64      %r135


uint_2_double:
.L88:
	<entry-point>
	ucvtf.64    %r138 <- (32) %arg1
	ret.64      %r138


long_2_double:
.L90:
	<entry-point>
	scvtf.64    %r141 <- (64) %arg1
	ret.64      %r141


ulong_2_double:
.L92:
	<entry-point>
	ucvtf.64    %r144 <- (64) %arg1
	ret.64      %r144


float_2_double:
.L94:
	<entry-point>
	fcvtf.64    %r147 <- (32) %arg1
	ret.64      %r147


float_2_float:
.L96:
	<entry-point>
	ret.32      %arg1


double_2_double:
.L98:
	<entry-point>
	ret.64      %arg1


 * check-output-end
 */
