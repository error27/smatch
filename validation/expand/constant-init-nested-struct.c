struct s {
	int a;
	struct {
		int b;
		int c;
	} s;
};

int foo(void)
{
	struct s s = {1, {2, 3}};
	return s.s.c;
}

/*
 * check-name: constant-init-nested-struct
 * check-command: test-linearize -Wno-decl -fdump-ir $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-contains: phisrc\\..*\\$3
 * check-output-excludes: load\\.
 */
