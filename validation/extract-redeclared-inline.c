/*
 * check-name: extract redeclared inline
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
 * extract: wrote $file.extract
 * check-output-end
 */

static inline int increment(int value)
{
	return value + 1;
}

static inline int increment(int value);

int call_increment(int value)
{
	return increment(value);
}
