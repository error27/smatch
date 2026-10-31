int start(int x);
struct ops {
	typeof(start) *start;
};
extern const struct ops *test_ops;

int test(void)
{
	return test_ops->start(1);
}

/*
 * check-name: extract struct member with typeof a function
 * check-command: extract $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-typeof-function-member.c.extract
 * check-output-end
 */
