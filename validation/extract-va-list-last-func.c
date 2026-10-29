/*
 * check-name: extract va_list for last function
 * check-command: extract --last-func $file && grep -Fqx 'typedef __builtin_va_list va_list;' $file.extract.last && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-va-list-last-func.c.extract.last
 * check-output-end
 */

typedef __builtin_va_list va_list;

int call(va_list args)
{
	return 0;
}
