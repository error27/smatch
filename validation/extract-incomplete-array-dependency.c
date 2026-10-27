/*
 * check-name: extract incomplete array dependency
 * check-command: extract $file && grep -Fqx 'static char modes[] = "file";' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-incomplete-array-dependency.c.extract
 * check-output-end
 */

static char modes[] = "file";

static inline int mode_count(void)
{
	return sizeof(modes) - 1;
}

int get_mode_count(void)
{
	return mode_count();
}
