struct s {
	struct {
		int u, v;
	} a, b;
};

static const struct s s;

static int foo(int c)
{
	return c && s.b.v;
}

/*
 * check-name: cost-deref-nested
 * check-command: test-linearize -fdump-ir $file
 *
 * check-output-ignore
 * check-output-excludes: cbr
 */
