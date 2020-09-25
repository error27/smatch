struct s {
	__INT32_TYPE__ x;
	__INT16_TYPE__ y;
	unsigned char f[];
};

static int foo(struct s *s)
{
	return __builtin_offsetof(typeof(*s), f);
}

/*
 * check-name: flex-array-padding
 * check-command: test-linearize -Wflexible-array-padding $file
 * check-known-to-fail
 *
 * check-output-ignore
 *
 * check-error-start
flex-array-padding.c:4:23: warning: flexible array member has padding
 * check-error-end
 */
