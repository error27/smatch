#include <linux/slab.h>
#include <linux/spinlock.h>
#include "../../check_debug.h"

spinlock_t lock;

void queue_event(void *event);
void queue_event(void *event)
{
	int a = 0;

	scoped_guard(spinlock_irqsave, &lock) {
		kfree(event);
		a++;
	}
	__smatch_implied(a);
	kfree(event);
}

/*
 * check-name: smatch: scoped_guard #1
 * check-command: validation/kernel/build.sh sm_scoped_guard1.c
 *
 * check-output-start
sm_scoped_guard1.c:16 queue_event() implied: a = '1'
sm_scoped_guard1.c:17 queue_event() error: double free of 'event' (line 13)
 * check-output-end
 */
