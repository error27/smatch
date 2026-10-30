/*
 * check-name: extract va builtins
 * check-command: extract $file && grep -Fq 'va_list *va;' $file.extract && grep -Fq '__builtin_va_start(args, format);' $file.extract && grep -Fq '__builtin_va_copy(copy, args);' $file.extract && grep -Fq '__builtin_va_arg(args, unsigned int)' $file.extract && grep -Fq '__builtin_va_arg(args, char *)' $file.extract && grep -Fq '__builtin_va_end(args);' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-va-builtins.c.extract
 * check-output-end
 */

typedef __builtin_va_list va_list;

extern int render(va_list args);

struct va_holder {
	va_list *va;
};

int call(const char *format, ...)
{
	va_list args;
	va_list copy;
	struct va_holder holder = { .va = &args };
	unsigned int value;
	char *pointer;
	int ret;

	__builtin_va_start(args, format);
	__builtin_va_copy(copy, args);
	value = __builtin_va_arg(args, unsigned int);
	pointer = __builtin_va_arg(args, char *);
	ret = render(*holder.va);
	__builtin_va_end(copy);
	__builtin_va_end(args);
	return ret + value + (pointer != 0);
}
