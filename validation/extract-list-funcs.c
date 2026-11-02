struct obj { int val; };

# 1 "fake-header.h" 1
static inline int header_func(struct obj *o)
{
	return o->val;
}
# 7 "extract-list-funcs.c" 2

static int helper(struct obj *o)
{
	return header_func(o) + 1;
}

int last(struct obj *o)
{
	return helper(o) * 2;
}

/*
 * check-name: extract --list-funcs prints the functions in the .c file
 * check-command: extract --list-funcs $file && ! test -e $file.extract.last
 *
 * check-output-start
helper
last
 * check-output-end
 */
