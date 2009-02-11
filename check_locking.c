/*
 * sparse/check_locking.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

/*
 * For this test let's look for functions that return a negative value
 * with a spinlock held.
 *
 * One short coming is that it assumes a function isn't supposed
 * to return negative with a lock held.  Perhaps the function was
 * called with the lock held.  A more complicated script could check that.
 *
 */

#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"

static const char *lock_funcs[] = {
	"_spin_lock",
	"_spin_lock_irqsave",
	"_spin_lock_irq",
	"_spin_lock_bh",
	"_spin_lock_nested",
	"_spin_lock_irqsave_nested",
	"_raw_spin_lock",
	"_read_lock",
	"_read_lock_irqsave",
	"_read_lock_irq",
	"_read_lock_bh",
	"_write_lock",
	"_write_lock_irqsave",
	"_write_lock_irq",
	"_write_lock_bh",
	"down",
	NULL,
};

static const char *unlock_funcs[] = {
	"_spin_unlock",
	"_spin_unlock_irqrestore",
	"_spin_unlock_irq",
	"_spin_unlock_bh",
	"_raw_spin_unlock",
	"_read_unlock",
	"_read_unlock_irqrestore",
	"_read_unlock_irq",
	"_read_unlock_bh",
	"_write_unlock",
	"_write_unlock_irqrestore",
	"_write_unlock_irq",
	"_write_unlock_bh",
	"up",
	NULL,
};

struct locked_call {
	const char *function;
	const char *lock;
};

static struct locked_call lock_needed[] = { 
	{"tty_ldisc_ref_wait", "tty_ldisc_lock"},
};

static int my_id;

STATE(locked);
STATE(unlocked);

/*
 * merge_func() can go away when we fix the core to just store all the possible 
 * states.
 *
 * The parameters are passed in alphabetical order with NULL at the beginning
 * of the alphabet.  (s2 is never NULL).
 */

static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	if (s1 == NULL)
		return s2;
	return &undefined;

}

static char kernel[] = "kernel";
static char *match_lock_func(char *fn_name, struct expression_list *args)
{
	struct expression *lock_expr;
	int i;

	for (i = 0; lock_funcs[i]; i++) {
		if (!strcmp(fn_name, lock_funcs[i])) {
			lock_expr = get_argument_from_call_expr(args, 0);
			return get_variable_from_expr(lock_expr, NULL);
		}
	}
	if (!strcmp(fn_name, "lock_kernel"))
		return kernel;
	return NULL;
}

static char *match_unlock_func(char *fn_name, struct expression_list *args)
{
	struct expression *lock_expr;
	int i;

	for (i = 0; unlock_funcs[i]; i++) {
		if (!strcmp(fn_name, unlock_funcs[i])) {
			lock_expr = get_argument_from_call_expr(args, 0);
			return get_variable_from_expr(lock_expr, NULL);
		}
	}
	if (!strcmp(fn_name, "unlock_kernel"))
		return kernel;
	return NULL;
}

static void check_locks_needed(const char *fn_name)
{
	struct smatch_state *state;
	int i;

	for (i = 0; i < sizeof(lock_needed)/sizeof(struct locked_call); i++) {
		if (!strcmp(fn_name, lock_needed[i].function)) {
			state = get_state(lock_needed[i].lock, my_id, NULL);
			if (state != &locked) {
				smatch_msg("%s called without holding %s lock",
					lock_needed[i].function,
					lock_needed[i].lock);
			}
		}
	}
}

static void match_call(struct expression *expr)
{
	char *fn_name;
	char *lock_name;

	fn_name = get_variable_from_expr(expr->fn, NULL);
	if (!fn_name)
		return;

	if ((lock_name = match_lock_func(fn_name, expr->args)))
		set_state(lock_name, my_id, NULL, &locked);
	else if ((lock_name = match_unlock_func(fn_name, expr->args)))
		set_state(lock_name, my_id, NULL, &unlocked);
	else
		check_locks_needed(fn_name);
	free_string(fn_name);
	return;
}

static void match_condition(struct expression *expr)
{
	/* __raw_spin_is_locked */
}

static int possibly_negative(struct expression *expr)
{
	char *name;
	struct symbol *sym;
	struct state_list *slist;
	struct sm_state *tmp;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		return 0;
	slist = get_possible_states(name, SMATCH_EXTRA, sym);
	FOR_EACH_PTR(slist, tmp) {
		int value = 0;
		
		if (tmp->state->data) 
			value =  *(int *)tmp->state->data;

		if (value < 0) {
			return 1;
		}
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

static void match_return(struct statement *stmt)
{
	int ret_val;
	struct state_list *slist;
	struct sm_state *tmp;

	ret_val = get_value(stmt->ret_value);
	if (ret_val >= 0) {
		return;
	}
	if (ret_val == UNDEFINED) {
		if (!possibly_negative(stmt->ret_value))
			return;
	}

	slist = get_all_states(my_id);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state != &unlocked)
			smatch_msg("returned negative with %s lock held",
				   tmp->name);
	} END_FOR_EACH_PTR(tmp);
}

void register_locking(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&match_return, RETURN_HOOK);
}
