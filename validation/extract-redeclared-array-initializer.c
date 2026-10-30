const char banner[] __attribute__((weak));
const char banner[] = "banner";

int target(void)
{
	return banner[0];
}

/*
 * check-name: extract redeclared array initializer
 * check-command: extract $file && grep -Fqx 'const char banner[] = "banner";' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-redeclared-array-initializer.c.extract
 * check-output-end
 */
