extern void def(void *, unsigned int);

static int bar(void)
{
	int x[2] = { 1, 2 };

	def(x, sizeof(x));
	return x[1];
}

/*
 * check-name: eval/addressable-degen
 * check-command: test-linearize -fdump-ir $file
 *
 * check-output-ignore
 * check-output-contains: load\\.
 */
