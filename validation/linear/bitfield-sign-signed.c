struct s {
	int f:2;
};

static int getf(struct s s) { return s.f; }

/*
 * check-name: bitfield-sign-signed
 * check-command: test-linearize -fdump-ir=linearize -fsigned-bitfields $file
 *
 * check-output-ignore
 * check-output-contains: sext\\.
 */
