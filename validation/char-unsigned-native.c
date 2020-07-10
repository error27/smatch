#define	MASK ((1 << __CHAR_BIT__) - 1)

void foo(void)
{
	_Static_assert((char) -1 == (-1 & MASK), "plain char is not unsigned");
}

/*
 * check-name: char-unsigned-native
 * check-command: sparse --arch=arm -Wno-decl $file
 */
