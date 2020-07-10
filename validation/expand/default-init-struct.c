struct s {
	int a;
	int b;
	int c;
};


int test_struct(void)
{
	struct s s = { .a = 1, .c = 3, };

	return s.b;
}

/*
 * check-name: default-init-struct
 * check-command: test-linearize -Wno-decl -fdump-ir $file
 *
 * check-output-ignore
 * check-output-contains: phisrc\\..*return.*\\$0
 * check-output-excludes: load\\.
 */
