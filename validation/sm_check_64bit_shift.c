void test_shift(unsigned long long *result, unsigned int shift)
{
	*result = 1U << shift;
}

void test_ok(unsigned long long *result, unsigned int shift)
{
	*result = 1ULL << shift;
}

/*
 * check-name: smatch check 64-bit shift
 * check-command: smatch --no-data --spammy sm_check_64bit_shift.c
 *
 * check-output-start
sm_check_64bit_shift.c:3 test_shift() warn: should '1 << shift' be a 64 bit type?
 * check-output-end
 */
