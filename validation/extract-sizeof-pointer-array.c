struct clk;

struct dev {
	struct clk *clk[4];
};

int test(struct dev *d, int i)
{
	return i < sizeof(d->clk) / sizeof(d->clk[0]);
}

/*
 * check-name: extract sizeof() of an array of pointers to an opaque struct
 * check-command: extract --func=test $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-sizeof-pointer-array.c.extract.test
 * check-output-end
 */
