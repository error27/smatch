struct s { int x; };

static int helper(struct s *p)
{
	return p->x;
}

static int unused(void)
{
	return 1;
}

int foo(struct s *p)
{
	return helper(p);
}

int bar(void)
{
	return helper(0) + 2;
}

/*
 * check-name: extract-func-list
 * check-command: extract --func=foo,bar,helper $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract.foo,bar,helper && grep -c -e "^static int helper(" -e "^int foo(" -e "^int bar(" -e "unused" $file.extract.foo,bar,helper
 *
 * check-output-start
extract: wrote extract-func-list.c.extract.foo,bar,helper
3
 * check-output-end
 */
