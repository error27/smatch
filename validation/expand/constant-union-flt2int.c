union u {
	int i;
	float f;
};

static int foo(void)
{
	union u u = { .f = 0.123 };
	return u.i;
}

/*
 * check-name: constant-union-float-to-int
 * check description: must not infer the int value from the float
 * check-command: test-linearize -fdump-ir $file
 *
 * check-output-ignore
 * check-output-pattern(1): setfval\\.
 * check-output-pattern(1): load\\.
 */
