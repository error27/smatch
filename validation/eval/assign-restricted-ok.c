#ifdef __CHECKER__
#define __bitwise __attribute__((bitwise))
#else
#define __bitwise
#endif

typedef __INT16_TYPE__ __bitwise __be16;

static __be16 foo(void)
{
	__be16 val = 0;
	return val;
}

/*
 * check-name: assign-restricted-ok
 * check-command: test-linearize -fdump-ir $file
 *
 * check-output-ignore
 * check-output-contains: store\\.16
 * check-output-excludes: store\\.32
 */
