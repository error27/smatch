#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sprintf.h>

#include "../../check_debug.h"

struct whatever {
	int a, b, c;
};

int frob(struct whatever *p);
int frob(struct whatever *p)
{
	memset(p, 0, sizeof(*p));
	__smatch_implied(p->a);
	return 0;
}


/*
 * check-name: smatch: memset #1
 * check-command: validation/kernel/build.sh sm_memset1.c
 *
 * check-output-start
sm_memset1.c:18 frob() implied: p->a = '0'
 * check-output-end
 */
