/*
 * check-name: extract va_list
 * check-command: extract $file && grep -Fqx 'typedef __builtin_va_list va_list;' $file.extract && grep -Fqx 'extern int render(va_list args);' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-va-list.c.extract
 * check-output-end
 */

typedef __builtin_va_list va_list;

extern int render(va_list args);

int call(va_list args)
{
	va_list copy = args;

	return render(copy);
}
