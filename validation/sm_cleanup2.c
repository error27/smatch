#include "check_debug.h"
#include "smatch.h"

char *get_pointer(void);
int frob(void);

void func(int a)
{
	{
		char __cleanup_marker *a = "foo";

		switch (a) {
		case 1: {
			char __cleanup_marker *b = "foo";
			__smatch_note("break: cleanup b");
			break;
		}
		case 2:
			frob();
			char __cleanup_marker *c = "foo";
			__smatch_note("end of switch: cleanup c");
		}
		__smatch_note("done block: cleanup a");
	}
}

/*
 * check-name: smatch: cleanup #2
 * check-command: smatch -I.. sm_cleanup2.c
 *
 * check-output-start
sm_cleanup2.c:15 func() break: cleanup b
sm_cleanup2.c:16 func() cleanup: b: "foo"
sm_cleanup2.c:21 func() end of switch: cleanup c
sm_cleanup2.c:21 func() cleanup: c: "foo"
sm_cleanup2.c:23 func() done block: cleanup a
sm_cleanup2.c:23 func() cleanup: a: "foo"
 * check-output-end
 */
