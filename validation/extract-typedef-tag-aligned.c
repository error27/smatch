typedef struct { char c; } tdal_t __attribute__((aligned(8)));

int test(tdal_t *t, int x)
{
	/* GCC fails with a duplicate case value if a size is wrong */
	switch (x) {
	case 1:
	case sizeof(tdal_t) != 1 || _Alignof(tdal_t) != 8:
		return 0;
	}
	return t->c;
}

/*
 * check-name: extract keeps aligned() on a typedef of a tag
 * check-command: extract --func=test $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-typedef-tag-aligned.c.extract.test
 * check-output-end
 */
