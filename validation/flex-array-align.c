struct s {
	__INT32_TYPE__ x;
	__INT16_TYPE__ y;
	unsigned char f[];
};

static int foo(struct s *s)
{
	return (sizeof(*s) << 16) | __builtin_offsetof(typeof(*s), f);
}

/*
 * check-name: flex-array-align
 * check-command: test-linearize -Wno-flexible-array-sizeof $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..*\\$0x80006
 */
