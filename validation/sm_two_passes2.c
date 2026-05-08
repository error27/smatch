#include "check_debug.h"

void free(void *p);

struct my_struct {
	struct my_struct *next;
	int a, b, c;
};

extern struct my_struct *list;

void func(void)
{
	struct my_struct *p, *next;

	for (p = list; p && (next = p->next, 1); p = next)
		free(p);
}

/*
 * check-name: smatch: two passes #2
 * check-command: smatch -I.. sm_two_passes2.c
 */
