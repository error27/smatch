/*
 * check-name: extract external inline static call
 * check-command: extract $file && gcc -Werror -Wall -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-external-inline-static-call.c.extract
 * check-output-end
 */

static void helper(void)
{
}

inline __attribute__((__gnu_inline__)) __attribute__((__unused__))
void call_helper(void)
{
	helper();
}

static void use_helper(void)
{
	call_helper();
}
