extern int target(void);
extern typeof(target) alias;

int target(void)
{
	return alias();
}

/*
 * check-name: extract duplicate extern typeof declaration
 * check-command: extract $file && test "$(grep -cx 'extern typeof(target) alias;' $file.extract)" = 1 && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-duplicate-extern-typeof.c.extract
 * check-output-end
 */
