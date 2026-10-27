/*
 * check-name: extract va builtins
 * check-command: extract $file && grep -Fq '__builtin_va_start(args, format);' $file.extract && grep -Fq '__builtin_va_copy(copy, args);' $file.extract && grep -Fq '__builtin_va_end(args);' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-va-builtins.c.extract
 * check-output-end
 */

typedef __builtin_va_list va_list;

extern int render(va_list args);

int call(const char *format, ...)
{
	va_list args;
	va_list copy;
	int ret;

	__builtin_va_start(args, format);
	__builtin_va_copy(copy, args);
	ret = render(copy);
	__builtin_va_end(copy);
	__builtin_va_end(args);
	return ret;
}
