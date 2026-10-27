/*
 * check-name: extract va_list
 * check-command: extract $file && grep -Fqx 'typedef __builtin_va_list va_list;' $file.extract && grep -Fqx 'extern int render_size(char *buf, unsigned long size, const char *fmt, va_list args);' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-va-list.c.extract
 * check-output-end
 */

typedef __builtin_va_list va_list;
typedef unsigned long size_t;

extern int render(va_list args);
extern int render_size(char *buf, size_t size, const char *fmt, va_list args);

int call(va_list args)
{
	return render(args) + render_size((char *)0, 0, "", args);
}
