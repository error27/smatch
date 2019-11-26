void foo(void)
{
	_Static_assert((char) -1 == -1, "plain char is not signed");
}

/*
 * check-name: char-signed-native
 * check-command: sparse --arch=i386 -Wno-decl $file
 */
