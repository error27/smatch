union u {
	int i;
	float f;
};

static float foo(void)
{
	union u u = { .i = 3 };
	return u.f;
}

/*
 * check-name: constant-union-int-to-float
 * check description: must not infer the float value from the int
 * check-command: test-linearize -fdump-ir $file
 *
 * check-output-ignore
 * check-output-pattern(1): load\\.
 */
