typedef unsigned int uint32_t;

void use_uint(unsigned int *p);

static void test(void)
{
	uint32_t val = 1;
	unsigned short short_val = 1;

	use_uint((unsigned int *)&val);
	use_uint((unsigned int *)&short_val);
}

/*
 * check-name: smatch same type endian cast
 * check-command: smatch sm_endian_cast.c | grep endianness
 *
 * check-output-start
sm_endian_cast.c:11 test() warn: does endianness matter for 'short_val'?
 * check-output-end
 */
