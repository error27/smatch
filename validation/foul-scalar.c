#define __bitwise __attribute__((bitwise))

typedef unsigned short __bitwise __be16;

static void foo(__be16 x)
{
	if (~x)
		;
}

/*
 * check-name: foul-scalar
 */
