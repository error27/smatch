#define SHIFT 3

unsigned long test(void)
{
	return (1UL << SHIFT) | (1UL << 2);
}

/*
 * check-name: smatch: check bit shift
 * check-command: smatch --no-data --info sm_check_bit_shift.c | grep 'info: bit shifter'
 *
 * check-output-start
sm_check_bit_shift.c:5 test() info: bit shifter 'SHIFT' '3'
 * check-output-end
 */
