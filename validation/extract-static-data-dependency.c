/*
 * check-name: extract static data dependency
 * check-command: extract $file && ! grep -q '^extern int cache;' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-static-data-dependency.c.extract
 * check-output-end
 */

static int cache;

static inline int read_cache(void)
{
	return cache;
}

int use_cache(void)
{
	return read_cache();
}
