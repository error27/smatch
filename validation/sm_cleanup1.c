#include "check_debug.h"
#include "smatch.h"

char *get_pointer(void);
int frob(void);

void func(void)
{
	char __cleanup_marker *a = "foo";

	for (char __cleanup_marker *b = get_pointer(); frob();) {
		char __cleanup_marker *c = "foo";

		if (i == 0) {
			__smatch_note("goto: do nothing");
			goto next;
		}
		if (i == 1) {
			__smatch_note("goto: cleanup c");
			continue;
		}
		if (i == 2) {
			__smatch_note("goto: cleanup b, c");
			goto out;
		}
next:
		if (i == 3) {
			__smatch_note("break: cleanup c");
			continue;
		}
		if (frob()) {
			__smatch_note("break: cleanup b, c");
			break;
		}
		if (frob()) {
			__smatch_note("cleanup a, b, c");
			return;
		}
		__smatch_note("end iter block. cleanup c. and then later b");
	}
out:
	__smatch_note("done function. cleanup a");
}


/*
 * check-name: smatch: cleanup #1
 * check-command: smatch -I.. sm_cleanup1.c
 *
 * check-output-start
sm_cleanup1.c:15 func() goto: do nothing
sm_cleanup1.c:19 func() goto: cleanup c
sm_cleanup1.c:20 func() cleanup: c: "foo"
sm_cleanup1.c:23 func() goto: cleanup b, c
sm_cleanup1.c:24 func() cleanup: b: get_pointer()
sm_cleanup1.c:24 func() cleanup: c: "foo"
sm_cleanup1.c:28 func() break: cleanup c
sm_cleanup1.c:29 func() cleanup: c: "foo"
sm_cleanup1.c:32 func() break: cleanup b, c
sm_cleanup1.c:33 func() cleanup: b: get_pointer()
sm_cleanup1.c:33 func() cleanup: c: "foo"
sm_cleanup1.c:36 func() cleanup a, b, c
sm_cleanup1.c:37 func() cleanup: a: "foo"
sm_cleanup1.c:37 func() cleanup: b: get_pointer()
sm_cleanup1.c:37 func() cleanup: c: "foo"
sm_cleanup1.c:39 func() end iter block. cleanup c. and then later b
sm_cleanup1.c:39 func() cleanup: c: "foo"
sm_cleanup1.c:11 func() cleanup: b: get_pointer()
sm_cleanup1.c:42 func() done function. cleanup a
sm_cleanup1.c:42 func() cleanup: a: "foo"
 * check-output-end
 */
