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
	"__raw_spin_lock",
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
	"__raw_spin_unlock",
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

/* These functions return 0 on success and negative on failure */
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
STATE(start_state);
STATE(unlocked);

static struct smatch_state *get_start_state(struct sm_state *sm)
{
       int is_locked = 0;
       int is_unlocked = 0;

       if (in_tracker_list(starts_locked, my_id, sm->name, sm->sym))
               is_locked = 1;
       if (in_tracker_list(starts_unlocked, my_id, sm->name, sm->sym))
               is_unlocked = 1;
       if (is_locked && is_unlocked)
               return &undefined;
       if (is_locked)
               return &locked;
       if (is_unlocked)
	       return &unlocked;
	return &undefined;
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return &start_state;
}

static char *get_lock_name(struct expression *expr, void *data)
{
	struct expression *lock_expr;

	if (data) {
		return alloc_string((char *)data);
	} else {
		lock_expr = get_argument_from_call_expr(expr->args, 0);
		return get_variable_from_expr(lock_expr, NULL);
	}
}

static void match_lock_func(const char *fn, struct expression *expr, void *data)
{
	char *lock_name;
	struct sm_state *sm;

	lock_name = get_lock_name(expr, data);
	if (!lock_name)
		return;
	sm = get_sm_state(my_id, lock_name, NULL);
	if (!sm)
		add_tracker(&starts_unlocked, my_id, lock_name, NULL);
	if (sm && slist_has_state(sm->possible, &locked))
		sm_msg("error: double lock '%s'", lock_name);
	set_state(my_id, lock_name, NULL, &locked);
	free_string(lock_name);
}

static void match_unlock_func(const char *fn, struct expression *expr,
			      void *data)
{
	char *lock_name;
	struct sm_state *sm;

	lock_name = get_lock_name(expr, data);
	if (!lock_name)
		return;
	sm = get_sm_state(my_id, lock_name, NULL);
	if (!sm)
		add_tracker(&starts_locked, my_id, lock_name, NULL);
	if (sm && slist_has_state(sm->possible, &unlocked))
		sm_msg("error: double unlock '%s'", lock_name);
	set_state(my_id, lock_name, NULL, &unlocked);
	free_string(lock_name);
}

static void match_lock_failed(const char *fn, struct expression *expr,
			struct expression *unused, void *data)
{
	char *lock_name;
	struct sm_state *sm;

	lock_name = get_lock_name(expr, data);
	if (!lock_name)
		return;
	sm = get_sm_state(my_id, lock_name, NULL);
	if (!sm)
		add_tracker(&starts_unlocked, my_id, lock_name, NULL);
	set_state(my_id, lock_name, NULL, &unlocked);
	free_string(lock_name);
}

static void match_lock_aquired(const char *fn, struct expression *expr,
			struct expression *unused, void *data)
{
	char *lock_name;
	struct sm_state *sm;

	lock_name = get_lock_name(expr, data);
	if (!lock_name)
		return;
	sm = get_sm_state(my_id, lock_name, NULL);
	if (!sm)
		add_tracker(&starts_unlocked, my_id, lock_name, NULL);
	if (sm && slist_has_state(sm->possible, &locked))
		sm_msg("error: double lock '%s'", lock_name);
	set_state(my_id, lock_name, NULL, &locked);
	free_string(lock_name);
}

static void match_lock_needed(const char *fn, struct expression *expr,
			      void *data)
{
	struct smatch_state *state;
	char *fn_name;

	state = get_state(my_id, (char *)data, NULL);
	if (state == &locked) 
		return;
	fn_name = get_variable_from_expr(expr->fn, NULL);
	if (!fn_name) {
		sm_msg("Internal error.");
		exit(1);
	}
	sm_msg("error: %s called without holding '%s' lock", fn_name,
		   (char *)data);
	free_string(fn_name);
}

static void match_locks_on_non_zero(const char *fn, struct expression *expr,
				   void *data)
{
	char *lock_name;
	struct sm_state *sm;

	lock_name = get_lock_name(expr, data);
	if (!lock_name)
		return;
	sm = get_sm_state(my_id, lock_name, NULL);
	if (!sm)
		add_tracker(&starts_unlocked, my_id, lock_name, NULL);
	set_true_false_states(my_id, lock_name, NULL, &locked, &unlocked);
	free_string(lock_name);
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
		if (tmp->state == &unlocked)
			isunlocked = 1;
		if (tmp->state == &start_state) {
			struct smatch_state *s;

			s = get_start_state(tmp);
			if (s == &locked)
				islocked = 1;
			else if (s == &unlocked)
				isunlocked = 1;
			else
				undef = 1;
		}		
		if (tmp->state == &undefined)
			undef = 1;  // i don't think this is possible any more.
	} END_FOR_EACH_PTR(tmp);
	if ((islocked && isunlocked) || undef)
		sm_msg("warn: '%s' is sometimes locked here and "
			   "sometimes unlocked.", sm->name);
}

static void match_return(struct expression *ret_value)
{
	struct locks_on_return *ret;
	struct state_list *slist;
	struct sm_state *tmp;

	if (!final_pass)
		return;

	ret = alloc_return(get_lineno());

	slist = get_all_states(my_id);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == &locked) {
			add_tracker(&ret->locked, tmp->owner, tmp->name, 
				tmp->sym);
		} else if (tmp->state == &unlocked) {
			add_tracker(&ret->unlocked, tmp->owner, tmp->name, 
				tmp->sym);
		} else if (tmp->state == &start_state) {
			struct smatch_state *s;

			s = get_start_state(tmp);
			if (s == &locked)
				add_tracker(&ret->locked, tmp->owner, tmp->name, 
					    tmp->sym);
			if (s == &unlocked)
				add_tracker(&ret->unlocked, tmp->owner,tmp->name, 
					     tmp->sym);
		}else {
			check_possible(tmp);
		}
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
	add_ptr_list(&all_returns, ret);
}

static void print_inconsistent_returns(struct tracker *lock,
				struct smatch_state *start)
{
	struct locks_on_return *tmp;
	int i;

	sm_printf("%s +%d %s(%d) ", get_filename(), get_lineno(), get_function(), get_func_pos());
	sm_printf("warn: inconsistent returns %s:", lock->name);
	sm_printf(" locked (");
	i = 0;
	FOR_EACH_PTR(all_returns, tmp) {
		if (in_tracker_list(tmp->unlocked, lock->owner, lock->name, lock->sym))
			continue;
		if (in_tracker_list(tmp->locked, lock->owner, lock->name, lock->sym)) {
			if (i++)
				sm_printf(",");
			sm_printf("%d", tmp->line);
			continue;
		}
		if (start == &locked) {
			if (i++)
				sm_printf(",");
			sm_printf("%d", tmp->line);
		}
	} END_FOR_EACH_PTR(tmp);

	sm_printf(") unlocked (");
	i = 0;
	FOR_EACH_PTR(all_returns, tmp) {
		if (in_tracker_list(tmp->unlocked, lock->owner, lock->name, lock->sym)) {
			if (i++)
				sm_printf(",");
			sm_printf("%d", tmp->line);
			continue;
		}
		if (in_tracker_list(tmp->locked, lock->owner, lock->name, lock->sym)) {
			continue;
		}
		if (start == &unlocked) {
			if (i++)
				sm_printf(",");
			sm_printf("%d", tmp->line);
		}
	} END_FOR_EACH_PTR(tmp);
	sm_printf(")\n");
}

static void check_returns_consistently(struct tracker *lock,
				struct smatch_state *start)
{
	int returns_locked = 0;
	int returns_unlocked = 0;
	struct locks_on_return *tmp;

	FOR_EACH_PTR(all_returns, tmp) {
		if (in_tracker_list(tmp->unlocked, lock->owner, lock->name, 
					lock->sym))
			returns_unlocked = tmp->line;
		else if (in_tracker_list(tmp->locked, lock->owner, lock->name, 
						lock->sym))
			returns_locked = tmp->line;
		else if (start == &locked)
			returns_locked = tmp->line;
		else if (start == &unlocked)
			returns_unlocked = tmp->line;
	} END_FOR_EACH_PTR(tmp);

	if (returns_locked && returns_unlocked)
		print_inconsistent_returns(lock, start);
}

static void check_consistency(struct symbol *sym)
{
	struct tracker *tmp;

	if (is_reachable())
		match_return(NULL);

	FOR_EACH_PTR(starts_locked, tmp) {
		if (in_tracker_list(starts_unlocked, tmp->owner, tmp->name, 
					tmp->sym))
			sm_msg("error:  locking inconsistency.  We assume "
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

}

static void clear_lists(void)
{
	struct locks_on_return *tmp;

	free_trackers_and_list(&starts_locked);
	free_trackers_and_list(&starts_unlocked);

	FOR_EACH_PTR(all_returns, tmp) {
		free_trackers_and_list(&tmp->locked);
		free_trackers_and_list(&tmp->unlocked);
		free(tmp);
	} END_FOR_EACH_PTR(tmp);
	__free_ptr_list((struct ptr_list **)&all_returns);
}

static void match_func_end(struct symbol *sym)
{
	check_consistency(sym);
	clear_lists();
}

void check_locking(int id)
{
	int i;

	my_id = id;
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_func_end, END_FUNC_HOOK);

	for (i = 0; lock_funcs[i]; i++) {
		add_function_hook(lock_funcs[i], &match_lock_func, NULL);
	}
	add_function_hook("lock_kernel", &match_lock_func, (void *)"kernel");
	for (i = 0; unlock_funcs[i]; i++) {
		add_function_hook(unlock_funcs[i], &match_unlock_func, NULL);
	}
	add_function_hook("unlock_kernel", &match_unlock_func, (void *)"kernel");
	for (i = 0; i < sizeof(lock_needed)/sizeof(struct locked_call); i++) {
		add_function_hook(lock_needed[i].function, &match_lock_needed, 
				  (void *)lock_needed[i].lock);
	}

	for (i = 0; conditional_funcs[i]; i++) {
		add_conditional_hook(conditional_funcs[i],
				  &match_locks_on_non_zero, NULL);
	}

	for (i = 0; reverse_cond_funcs[i]; i++) {
		return_implies_state(reverse_cond_funcs[i], whole_range.min, -1,
				     &match_lock_failed, NULL);
		return_implies_state(reverse_cond_funcs[i], 0, 0,
				     &match_lock_aquired, NULL);

	}
}
