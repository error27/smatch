union u {
	char c;
	float f;
};

static int foo(void)
{
	union u u = { .f = 0.123 };
	return u.c;
}

/*
 * check-name: constant-union-size
 * check description: the size of the initializer doesn't match
 * check-command: test-linearize -fdump-ir $file
 *
 * check-output-ignore
 * check-output-contains: load\\.
 * check-output-excludes: ret\\..*\\$
 */
