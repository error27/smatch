/*
 * check-name: extract va_list typedef chain
 * check-command: extract $file && grep -Fqx 'typedef __builtin_va_list __gnuc_va_list;' $file.extract && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-gnuc-va-list.c.extract
 * check-output-end
 */

typedef __builtin_va_list __gnuc_va_list;
typedef __gnuc_va_list va_list;

extern int vprint(const char *fmt, __gnuc_va_list args);

int print(const char *fmt, va_list args)
{
	return vprint(fmt, args);
}
