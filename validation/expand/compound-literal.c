#define SAME_TYPE(A, B)	\
	__builtin_types_compatible_p(A, B)

struct s {
	int i;
};

static void foo(struct s *p)
{
	*p = (struct s) { .i = SAME_TYPE(int, int), };
}

/*
 * check-name: compound-literal
 * check-command: test-linearize $file
 *
 * check-output-start
foo:
.L0:
	<entry-point>
	store.32    $1 -> 0[%arg1]
	ret


 * check-output-end
 */
