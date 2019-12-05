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
 * check-name: type-punning-int-to-float
 * check description: must not infer the float value from the int
 * check-command: test-linearize $file
 *
 * check-output-ignore
 * check-output-contains: load\\.
 */
