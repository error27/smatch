struct obj { int val; };

# 1 "fake-header.h" 1
static inline int header_func(struct obj *o)
{
	return o->val;
}
# 7 "extract-all-funcs.c" 2

static int helper(struct obj *o)
{
	return header_func(o) + 1;
}

int last(struct obj *o)
{
	return helper(o) * 2;
}

/*
 * check-name: extract --all-funcs writes a file for each function in the .c file
 * check-command: extract --all-funcs $file && $default_path/extract --func=last $file > /dev/null && mv $file.extract.last $file.last && $default_path/extract --all-funcs $file > /dev/null && cmp $file.extract.last $file.last && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.helper && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-all-funcs.c.extract.helper
extract: wrote extract-all-funcs.c.extract.last
 * check-output-end
 */
