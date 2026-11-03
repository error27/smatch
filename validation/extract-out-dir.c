struct s { int x; };

static int helper(struct s *p)
{
	return p->x;
}

int foo(struct s *p)
{
	return helper(p);
}

/*
 * check-name: extract-out-dir
 * check-command: extract --out-dir=$file.out --func=foo ./$file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.out/$file
 *
 * check-output-start
extract: wrote extract-out-dir.c.out/extract-out-dir.c
 * check-output-end
 */
