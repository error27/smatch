struct tp { int x; int (*fn)(void); };

int iter(void);

struct tp tp_a __attribute__((__used__)) = { 1, iter };

int iter(void)
{
	return tp_a.x;
}

/*
 * check-name: extract does not put used on a forward declaration
 * check-command: extract $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-used-forward.c.extract
 * check-output-end
 */
