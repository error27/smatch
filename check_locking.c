/*
 * sparse/check_locking.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

/*
 * This test checks that locks are held the same across all returns.
 * 
 * Of course, some functions are designed to only hold the locks on success.
 * Oh well... We can rewrite it later if we want.
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
	"mutex_lock_nested",
	"mutex_lock",
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
	"mutex_unlock",
	NULL,
};

/* These are return 1 if they aquire the lock */
static const char *conditional_funcs[] = {
	"_spin_trylock",
	"_spin_trylock_bh",
	"_read_trylock",
	"_write_trylock",
	"generic__raw_read_trylock",
	"_raw_spin_trylock",
	"_raw_read_trylock",
	"_raw_write_trylock",
	"__raw_spin_trylock",
	"__raw_read_trylock",
	"__raw_write_trylock",
	"__raw_write_trylock",
	"mutex_trylock",
	NULL,
};

/* These functions return 0 on success */
static const char *reverse_cond_funcs[] = {
	"down_trylock",
	"down_interruptible",
	"mutex_lock_interruptible",
	"mutex_lock_interruptible_nested",
	"mutex_lock_killable",
	"mutex_lock_killable_nested",
	NULL,
};

/* todo still need to handle__raw_spin_is_locked */


struct locked_call {
	const char *function;
	const char *lock;
};

static struct locked_call lock_needed[] = { 
	{"tty_ldisc_ref_wait", "tty_ldisc_lock"},
};

static int my_id;

static struct tracker_list *starts_locked;
static struct tracker_list *starts_unlocked;

struct locks_on_return {
	int line;
	struct tracker_list *locked;
	struct tracker_list *unlocked;
};
DECLARE_PTR_LIST(return_list, struct locks_on_return);
static struct return_list *all_returns;

STATE(locked);
STATE(unlocked);

/*
 * merge_func() is used merging NULL and a state should be merge plus
 * the state that the function was originally called with.  This way
 * we can sometimes avoid being &undefined.
 */
static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	int is_locked = 0;
	int is_unlocked = 0;

	if (s1)
		return &merged;
	if (in_tracker_list(starts_locked, name, my_id, sym))
		is_locked = 1;
	if (in_tracker_list(starts_unlocked, name, my_id, sym))
		is_unlocked = 1;
	if (is_locked && is_unlocked)
		return &undefined;
	if (s2 == &locked && is_locked)
		return &locked;
	if (s2 == &unlocked && is_unlocked)
		return &unlocked;
	return &undefined;
}

static char *match_func(const char *list[], char *fn_name,
			struct expression_list *args)
{
	struct expression *lock_expr;
	int i;

	for (i = 0; list[i]; i++) {
		if (!strcmp(fn_name, list[i])) {
			lock_expr = get_argument_from_call_expr(args, 0);
			return get_variable_from_expr(lock_expr, NULL);
		}
	}
	return NULL;
}

static char kernel[] = "kernel";
static char *match_lock_func(char *fn_name, struct expression_list *args)
{
	char *arg;

	arg = match_func(lock_funcs, fn_name, args);
	if (arg)
		return arg;
	if (!strcmp(fn_name, "lock_kernel"))
		return alloc_string(kernel);
	return NULL;
}

static char *match_unlock_func(char *fn_name, struct expression_list *args)
{
	char *arg;

	arg = match_func(unlock_funcs, fn_name, args);
	if (arg)
		return arg;
	if (!strcmp(fn_name, "unlock_kernel"))
		return alloc_string(kernel);
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
				smatch_msg("error: %s called without holding '%s' lock",
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
	struct sm_state *sm;

	fn_name = get_variable_from_expr(expr->fn, NULL);
	if (!fn_name)
		return;

	if ((lock_name = match_lock_func(fn_name, expr->args))) {
		sm = get_sm_state(lock_name, my_id, NULL);
		if (!sm)
			add_tracker(&starts_unlocked, lock_name, my_id, NULL);
		if (sm && slist_has_state(sm->possible, &locked))
			smatch_msg("error: double lock '%s'", lock_name);
		set_state(lock_name, my_id, NULL, &locked);
	} else if ((lock_name = match_unlock_func(fn_name, expr->args))) {
		sm = get_sm_state(lock_name, my_id, NULL);
		if (!sm)
			add_tracker(&starts_locked, lock_name, my_id, NULL);
		if (sm && slist_has_state(sm->possible, &unlocked))
			smatch_msg("error: double unlock '%s'", lock_name);
		set_state(lock_name, my_id, NULL, &unlocked);
	} else
		check_locks_needed(fn_name);
	free_string(lock_name);
	free_string(fn_name);
	return;
}

static void match_condition(struct expression *expr)
{
	char *fn_name;
	char *lock_name;

	if (expr->type != EXPR_CALL)
		return;

	fn_name = get_variable_from_expr(expr->fn, NULL);
	if (!fn_name)
		return;

	if ((lock_name = match_func(conditional_funcs, fn_name, expr->args))) {
		if (!get_state(lock_name, my_id, NULL))
			add_tracker(&starts_unlocked, lock_name, my_id, NULL);
		set_true_false_states(lock_name, my_id, NULL, &locked, &unlocked);
	}
	if ((lock_name = match_func(reverse_cond_funcs, fn_name, expr->args))) {
		if (!get_state(lock_name, my_id, NULL))
			add_tracker(&starts_unlocked, lock_name, my_id, NULL);
		set_true_false_states(lock_name, my_id, NULL, &unlocked, &locked);
	}
	free_string(lock_name);
	free_string(fn_name);
	return;
}

static struct locks_on_return *alloc_return(int line)
{
	struct locks_on_return *ret;

	ret = malloc(sizeof(*ret));
	ret->line = line;
	ret->locked = NULL;
	ret->unlocked = NULL;
	return ret;
}

static void check_possible(struct sm_state *sm)
{
	struct sm_state *tmp;
	int islocked = 0;
	int isunlocked = 0;
	int undef = 0;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state == &locked)
			islocked = 1;
		else if (tmp->state == &unlocked)
			isunlocked = 1;
		else if (tmp->state == &undefined)
			undef = 1;
	} END_FOR_EACH_PTR(tmp);
	if ((islocked && isunlocked) || undef)
		smatch_msg("warn: '%s' is sometimes locked here and "
			   "sometimes unlocked.", sm->name);
}

static void match_return(struct statement *stmt)
{
	struct locks_on_return *ret;
	struct state_list *slist;
	struct sm_state *tmp;
	
	ret = alloc_return(get_lineno());

	slist = get_all_states(my_id);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == &locked) {
			add_tracker(&ret->locked, tmp->name, tmp->owner,
				tmp->sym);
		} else if (tmp->state == &unlocked) {
			add_tracker(&ret->unlocked, tmp->name, tmp->owner,
				tmp->sym);
		} else {
			check_possible(tmp);
		}
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
	add_ptr_list(&all_returns, ret);
}

static void check_returns_consistently(struct tracker *lock,
				struct smatch_state *start)
{
	int returns_locked = 0;
	int returns_unlocked = 0;
	struct locks_on_return *tmp;

	FOR_EACH_PTR(all_returns, tmp) {
		if (in_tracker_list(tmp->unlocked, lock->name, lock->owner,
					lock->sym))
			returns_unlocked = tmp->line;
		else if (in_tracker_list(tmp->locked, lock->name, lock->owner,
						lock->sym))
			returns_locked = tmp->line;
		else if (start == &locked)
			returns_locked = tmp->line;
		else if (start == &unlocked)
			returns_unlocked = tmp->line;
	} END_FOR_EACH_PTR(tmp);

	if (returns_locked && returns_unlocked)
		smatch_msg("warn: lock '%s' held on line %d but not on %d.",
			lock->name, returns_locked, returns_unlocked);

}

static void clear_lists()
{
	struct locks_on_return *tmp;

	free_trackers_and_list(&starts_locked);
	free_trackers_and_list(&starts_unlocked);

	FOR_EACH_PTR(all_returns, tmp) {
		free_trackers_and_list(&tmp->locked);
		free_trackers_and_list(&tmp->unlocked);
	} END_FOR_EACH_PTR(tmp);
	__free_ptr_list((struct ptr_list **)&all_returns);
}

static void check_consistency(struct symbol *sym)
{
	struct tracker *tmp;

	if (is_reachable())
		match_return(NULL);

	FOR_EACH_PTR(starts_locked, tmp) {
		if (in_tracker_list(starts_unlocked, tmp->name, tmp->owner,
					tmp->sym))
			smatch_msg("error:  locking inconsistency.  We assume "
				   "'%s' is both locked and unlocked at the "
				   "start.",
				tmp->name);
	} END_FOR_EACH_PTR(tmp);

	FOR_EACH_PTR(starts_locked, tmp) {
		check_returns_consistently(tmp, &locked);
	} END_FOR_EACH_PTR(tmp);

	FOR_EACH_PTR(starts_unlocked, tmp) {
		check_returns_consistently(tmp, &unlocked);
	} END_FOR_EACH_PTR(tmp);

	clear_lists();
}

void check_locking(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&check_consistency, END_FUNC_HOOK);
}
