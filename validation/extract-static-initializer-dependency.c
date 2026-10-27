/*
 * check-name: extract static initializer dependency
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-static-initializer-dependency.c.extract
 * check-output-end
 */

static int value;
static int *table[] = { &value };

static inline int read_value(void)
{
	return *table[0];
}
