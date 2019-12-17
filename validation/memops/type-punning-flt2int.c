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
 * check-name: type-punning-float-to-int
 * check description: must not infer the int value from the float
 * check-command: test-linearize $file
 *
 * check-output-ignore
 * check-output-contains: load\\.
 */
