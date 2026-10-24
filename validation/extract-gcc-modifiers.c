/*
 * check-name: extract GCC modifier list
 * check-command: extract $file && gcc -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
 * extract: wrote $file.extract
 * check-output-end
 */

static inline __attribute__((__gnu_inline__)) __attribute__((__pure__))
__attribute__((__unused__)) int helper(void)
{
	return 0;
}

int (*callback)(void) = helper;
