static int sfoo(void)
{
	return __builtin_types_compatible_p(char, signed char);
}

static int ufoo(void)
{
	return __builtin_types_compatible_p(char, unsigned char);
}

/*
 * check-name: plain-char-compatibility
 * check-command: test-linearize $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-pattern(2): ret.*\\$0
 * check-output-excludes: ret.*\\$1
 */
