/*
 * check-name: extract va_list in a macro expansion
 * check-command: extract $file && grep -Fqx 'typedef void (*trace_fn)(void *data, const char *fmt, va_list *va);' $file.extract && gcc -std=gnu11 -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-va-list-macro.c.extract
 * check-output-end
 */

typedef __builtin_va_list va_list;

struct func {
	void *func;
};

#define DECLARE(proto...)						\
	typedef void (*trace_fn)(void *data, proto);		\
	int iter(void *data, proto)				\
	{							\
		struct func *it = data;				\
		return ((void (*)(void *, proto))(it->func)) != 0; \
	}

DECLARE(const char *fmt, va_list *va)
