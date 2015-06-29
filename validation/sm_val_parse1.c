#include "check_debug.h"

int main(int x)
{
	__smatch_type_rl(int, "s32min-s32max[$2 + 4]", 5);

	return 0;
}
/*
 * check-name: smatch parse value
 * check-command: smatch -I.. sm_val_parse1.c
 *
 * check-output-start
sm_val_parse1.c:5 main() 's32min-s32max[$2 + 4]' => '9'
 * check-output-end
 */
