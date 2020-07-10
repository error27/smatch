#define typename(x) _Generic((x) 0,			\
_Bool:			"_Bool",			\
char:			"char",				\
unsigned char:		"unsigned char",		\
short:			"short",			\
unsigned short:		"unsigned short",		\
int:			"int",				\
unsigned int:		"unsigned int",			\
long:			"long",				\
unsigned long:		"unsigned long",		\
long long:		"long long",			\
unsigned long long:	"unsigned long long",		\
float:			"float",			\
double:			"double",			\
long double:		"long double",			\
void *:			"void *",			\
char *:			"char *",			\
int *:			"int *",			\
default:		"???")

#define TEST(name, x)	\
static const char *test_ ## name(void) { return typename(x); }

TEST(bool, _Bool)
TEST(char, char)
TEST(uchar, unsigned char)
TEST(short, short)
TEST(ushort, unsigned short)
TEST(int, int)
TEST(uint, unsigned int)
TEST(long, long)
TEST(ulong, unsigned long)
TEST(llong, long long)
TEST(ullong, unsigned long long)
TEST(float, float)
TEST(double, double)
TEST(ldouble, long double)
TEST(vptr, void *)
TEST(cptr, char *)
TEST(iptr, int *)
TEST(int128, __int128)

/*
 * check-name: generic-typename
 * check-command: test-linearize --arch=i386 -fsigned-char $file
 *
 * check-output-start
test_bool:
.L0:
	<entry-point>
	ret.32      "_Bool"


test_char:
.L2:
	<entry-point>
	ret.32      "char"


test_uchar:
.L4:
	<entry-point>
	ret.32      "unsigned char"


test_short:
.L6:
	<entry-point>
	ret.32      "short"


test_ushort:
.L8:
	<entry-point>
	ret.32      "unsigned short"


test_int:
.L10:
	<entry-point>
	ret.32      "int"


test_uint:
.L12:
	<entry-point>
	ret.32      "unsigned int"


test_long:
.L14:
	<entry-point>
	ret.32      "long"


test_ulong:
.L16:
	<entry-point>
	ret.32      "unsigned long"


test_llong:
.L18:
	<entry-point>
	ret.32      "long long"


test_ullong:
.L20:
	<entry-point>
	ret.32      "unsigned long long"


test_float:
.L22:
	<entry-point>
	ret.32      "float"


test_double:
.L24:
	<entry-point>
	ret.32      "double"


test_ldouble:
.L26:
	<entry-point>
	ret.32      "long double"


test_vptr:
.L28:
	<entry-point>
	ret.32      "void *"


test_cptr:
.L30:
	<entry-point>
	ret.32      "char *"


test_iptr:
.L32:
	<entry-point>
	ret.32      "int *"


test_int128:
.L34:
	<entry-point>
	ret.32      "???"


 * check-output-end
 */
