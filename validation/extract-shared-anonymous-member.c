struct engine {
	struct {
		unsigned long a;
		unsigned long b;
	} props, *defaults, saved[2];
	int x;
};

void test(struct engine *e)
{
	*e->defaults = e->props;
	e->saved[0] = e->props;
}

/*
 * check-name: extract members sharing an anonymous struct
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-shared-anonymous-member.c.extract.last
 * check-output-end
 */
