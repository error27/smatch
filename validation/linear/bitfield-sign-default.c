struct s {
	int f:2;
};

static int getf(struct s s) { return s.f; }

/*
 * check-name: bitfield-sign-default
 * check-command: test-linearize -fdump-ir=linearize $file
 *
 * check-output-ignore
 * check-output-contains: sext\\.
 */
