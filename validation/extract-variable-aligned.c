extern unsigned long volatile jif __attribute__((aligned(64)));
static char buf[3] __attribute__((aligned(16)));

int test(int x)
{
	int loc __attribute__((aligned(32))) = 1;

	/* GCC fails with a duplicate case value if an alignment is wrong */
	switch (x) {
	case 1:
	case _Alignof(jif) != 64 || _Alignof(buf) != 16 ||
	     _Alignof(loc) != 32:
		return 0;
	}
	return jif + buf[0] + loc;
}

/*
 * check-name: extract keeps aligned() on variables
 * check-command: extract --func=test $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-variable-aligned.c.extract.test
 * check-output-end
 */
