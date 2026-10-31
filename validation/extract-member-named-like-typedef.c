typedef signed char s8;
struct outer;
struct inner { int x; };
struct outer { struct inner s8; };

int test(struct outer *o)
{
	return o->s8.x;
}

/*
 * check-name: extract member named like a typedef
 * check-command: extract $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-member-named-like-typedef.c.extract
 * check-output-end
 */
