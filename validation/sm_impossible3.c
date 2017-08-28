#include "check_debug.h"

int a;

static void ad_agg_selection_logic(void)
{

	if (a < 0 || a > 2)
		return;

	switch (a) {
	case 1:
		break;
	default:
		__smatch_implied(a);
		__smatch_states("register_impossible");

	}

	switch (a) {
	case 0:
	case 1:
	case 2:
		break;
	default:
		__smatch_implied(a);
		__smatch_states("register_impossible");
	}

	switch (a) {
	case 0:
	case 1:
	case 2:
	default:
		__smatch_states("register_impossible");
	}

	switch (a) {
	case 4:
		__smatch_states("register_impossible");
	case 3:
	case 0:
		__smatch_states("register_impossible");
		break;
	case 1:
	case 2:
		__smatch_states("register_impossible");
		break;
	default:
		__smatch_states("register_impossible");
	}


}

/*
 * check-name: smatch impossible #3
 * check-command: smatch -I.. sm_impossible3.c
 *
 * check-output-start
sm_impossible3.c:15 ad_agg_selection_logic() implied: a = '0,2'
sm_impossible3.c:16 ad_agg_selection_logic() register_impossible: no states
sm_impossible3.c:26 ad_agg_selection_logic() implied: a = ''
sm_impossible3.c:27 ad_agg_selection_logic() [register_impossible] 'impossible' = 'impossible'
sm_impossible3.c:35 ad_agg_selection_logic() [register_impossible] 'impossible' = 'merged' (impossible, undefined, merged)
sm_impossible3.c:40 ad_agg_selection_logic() [register_impossible] 'impossible' = 'impossible'
sm_impossible3.c:43 ad_agg_selection_logic() [register_impossible] 'impossible' = 'merged' (impossible, undefined, merged)
sm_impossible3.c:47 ad_agg_selection_logic() [register_impossible] 'impossible' = 'merged' (impossible, undefined, merged)
sm_impossible3.c:50 ad_agg_selection_logic() [register_impossible] 'impossible' = 'impossible'
 * check-output-end
 */
