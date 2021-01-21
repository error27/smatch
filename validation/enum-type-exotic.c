enum foobar {
        C = (unsigned char)0,
        L = 1L,
};

unsigned int foo(void);
unsigned int foo(void)
{
#ifdef __CHECKER__
	_Static_assert([typeof(C)] == [enum foobar], "enum type");
	_Static_assert([typeof(C)] != [unsigned char], "char type");
#endif

	typeof(C) v = ~0;
	return v;
}

/*
 * check-name: enum-type-exotic
 * check-description:
 *	GCC type's for C is 'int' or maybe 'unsigned int'
 *	but certainly not 'unsigned char' like here.
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-excludes: ret\\.32 *\\$255
 */
