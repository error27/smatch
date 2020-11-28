union u {
	double d;
	int i[2];
};

void use(union u);

int foo(double x, double y)
{
	union u u;
	int r;

	u.d = x;
	r = u.i[0];
	u.d = y;

	use(u);
	return r;
}

/*
 * check-name: partial-load00
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: store\\.
 * check-output-contains: load\\.
 * check-output-returns: %r2
 */
