#define __bitwise __attribute__((bitwise))

typedef unsigned int __bitwise flag_t;

extern void consume_flags(flag_t flags);

void target(void)
{
	consume_flags(0);
}

/*
 * check-name: extract typedef before function declaration
 * check-command: extract --func=target $file && gcc -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-function-typedef-order.c.extract.target
 * check-output-end
 */
