struct key { int enabled; };
extern struct key k;

#define is_on(x) __builtin_types_compatible_p(typeof(*(&x)), struct key)

static inline int check(void)
{
	return is_on(k) ? k.enabled : 0;
}

static int first(void)
{
	return check();
}

int second(void)
{
	return check() + first();
}

/*
 * check-name: extract --all-funcs does not change typeof() for later functions
 * check-command: extract --all-funcs $file && $default_path/extract --func=second $file > /dev/null && mv $file.extract.second $file.second && $default_path/extract --all-funcs $file > /dev/null && cmp $file.extract.second $file.second && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.second
 *
 * check-output-start
extract: wrote extract-all-funcs-typeof.c.extract.first
extract: wrote extract-all-funcs-typeof.c.extract.second
 * check-output-end
 */
