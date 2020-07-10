#define typename(x) _Generic((x) 0,			\
char:			"char",				\
signed char:		"signed char",			\
unsigned char:		"unsigned char",		\
default:		"???")

#define TEST(name, x)	\
static const char *test_ ## name(void) { return typename(x); }

TEST(char, char)
TEST(schar, signed char)
TEST(uchar, unsigned char)

/*
 * check-name: generic-schar
 * check-command: test-linearize --arch=i386 -fsigned-char $file
 * check-known-to-fail
 *
 * check-output-start
test_char:
.L0:
	<entry-point>
	ret.32      "char"


test_schar:
.L2:
	<entry-point>
	ret.32      "signed char"


test_uchar:
.L4:
	<entry-point>
	ret.32      "unsigned char"


 * check-output-end
 */
