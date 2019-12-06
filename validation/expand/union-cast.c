union u {
	int i;
	struct s {
		int a;
	} s;
};

int foo(void)
{
	struct s s = { 3 };
	union u u = (union u)s;
	return u.s.a;
}

/*
 * check-name: union-cast
 * check-command: test-linearize -Wno-decl -fdump-ir $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-excludes: load\\.
 *
 * check-error-start
union-cast.c:11:22: warning: cast to non-scalar
union-cast.c:11:22: warning: cast from non-scalar
 * check-error-end
 */
