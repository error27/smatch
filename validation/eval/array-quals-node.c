struct s {
	int a;
	int b[3];
	int c[2][3];
};

struct c {
	const struct s s;
};

extern struct c v;

void f(void)
{
	  v.s.a = 0;
	 *v.s.b = 0;
	**v.s.c = 0;
}

/*
 * check-name: array-quals-node
 * check-known-to-fail
 *
 * check-error-start
eval/array-quals-node.c:15:14: error: assignment to const expression
eval/array-quals-node.c:16:14: error: assignment to const expression
eval/array-quals-node.c:17:14: error: assignment to const expression
 * check-error-end
 */
