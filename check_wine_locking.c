/*
 * sparse/check_wine_locking.c
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
 *
 * The list of wine locking functions came from an earlier script written
 * by Michael Stefaniuc.
 *
 */

#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(locked);
STATE(start_state);
STATE(unlocked);

enum action {
	LOCK,
	UNLOCK,
};

enum return_type {
	ret_any,
	ret_non_zero,
	ret_zero,
};

#define RETURN_VAL -1
#define NO_ARG -2

struct lock_info {
	const char *function;
	enum action action;
	const char *name;
	int arg;
	enum return_type return_type;
};

static struct lock_info wine_lock_table[] = {
	{"create_window_handle", LOCK, "create_window_handle", RETURN_VAL, ret_non_zero},
	{"WIN_GetPtr", LOCK, "create_window_handle", RETURN_VAL, ret_non_zero},
	{"WIN_ReleasePtr", UNLOCK, "create_window_handle", 0, ret_any},
	{"EnterCriticalSection", LOCK, "CriticalSection", 0, ret_any},
	{"LeaveCriticalSection", UNLOCK, "CriticalSection", 0, ret_any},
	{"RtlEnterCriticalSection", LOCK, "RtlCriticalSection", 0, ret_any},
	{"RtlLeaveCriticalSection", UNLOCK, "RtlCriticalSection", 0, ret_any},
	{"GDI_GetObjPtr", LOCK, "GDI_Get", 0, ret_non_zero},
	{"GDI_ReleaseObj", UNLOCK, "GDI_Get", 0, ret_any},
	{"LdrLockLoaderLock", LOCK, "LdrLockLoaderLock", 2, ret_any},
	{"LdrUnlockLoaderLock", UNLOCK, "LdrLockLoaderLock", 1, ret_any},
	{"_lock", LOCK, "_lock", 0, ret_any},
	{"_unlock", UNLOCK, "_lock", 0, ret_any},
	{"msiobj_lock", LOCK, "msiobj_lock", 0, ret_any},
	{"msiobj_unlock", UNLOCK, "msiobj_lock", 0, ret_any},
	{"RtlAcquirePebLock", LOCK, "PebLock", NO_ARG, ret_any},
	{"RtlReleasePebLock", UNLOCK, "PebLock", NO_ARG, ret_any},
	{"server_enter_uninterrupted_section", LOCK, "server_uninterrupted_section", 0, ret_any},
	{"server_leave_uninterrupted_section", UNLOCK, "server_uninterrupted_section", 0, ret_any},
	{"RtlLockHeap", LOCK, "RtlLockHeap", 0, ret_any},
	{"RtlUnlockHeap", UNLOCK, "RtlLockHeap", 0, ret_any},
	{"_EnterSysLevel", LOCK, "SysLevel", 0, ret_any},
	{"_LeaveSysLevel", UNLOCK, "SysLevel", 0, ret_any},
	{"USER_Lock", LOCK, "USER_Lock", NO_ARG, ret_any},
	{"USER_Unlock", UNLOCK, "USER_Lock", NO_ARG, ret_any},
	{"wine_tsx11_lock", LOCK, "wine_tsx11_lock", NO_ARG, ret_any},
	{"wine_tsx11_unlock", UNLOCK, "wine_tsx11_lock", NO_ARG, ret_any},
	{"wine_tsx11_lock_ptr", LOCK, "wine_tsx11_lock_ptr", NO_ARG, ret_any},
 	{"wine_tsx11_unlock_ptr", UNLOCK, "wine_tsx11_lock_ptr", NO_ARG, ret_any},
	{"wined3d_mutex_lock", LOCK, "wined3d_mutex_lock", NO_ARG, ret_any},
	{"wined3d_mutex_unlock", UNLOCK, "wined3d_mutex_lock", NO_ARG, ret_any},
	{"X11DRV_DIB_Lock", LOCK, "X11DRV_DIB_Lock", 0, ret_any},
	{"X11DRV_DIB_Unlock", UNLOCK, "X11DRV_DIB_Lock", 0, ret_any},
};

static struct lock_info kernel_lock_table[] = {
	{"__raw_spin_lock", LOCK, "spin_lock", 0, ret_any},
	{"__raw_spin_unlock", UNLOCK, "spin_lock", 0, ret_any},
	{"__raw_spin_trylock", LOCK, "spin_lock", 0, ret_non_zero},

	{"__spin_lock_nested", LOCK, "spin_lock", 0, ret_any},
	{"__spin_trylock", LOCK, "spin_lock", 0, ret_non_zero},
	{"__spin_lock", LOCK, "spin_lock", 0, ret_any},
	{"__spin_unlock", UNLOCK, "spin_lock", 0, ret_any},

 	{"__spin_lock_irqsave_nested", LOCK, "spin_lock", 0, ret_any},
 	{"__spin_lock_irqsave", LOCK, "spin_lock", 0, ret_any},
 	{"__spin_unlock_irqrestore", UNLOCK, "spin_lock", 0, ret_any},
 	{"__spin_lock_irq", LOCK, "spin_lock", 0, ret_any},
 	{"__spin_unlock_irq", UNLOCK, "spin_lock", 0, ret_any},

 	{"__spin_trylock_bh", LOCK, "spin_lock", 0, ret_non_zero},
 	{"__spin_lock_bh", LOCK, "spin_lock", 0, ret_any},
 	{"__spin_unlock_bh", UNLOCK, "spin_lock", 0, ret_any},
 	{"__raw_read_trylock", LOCK, "read_lock", 0, ret_non_zero},
 	{"__raw_read_trylock", LOCK, "read_lock", 0, ret_non_zero},
 	{"__read_trylock", LOCK, "read_lock", 0, ret_non_zero},
 	{"__read_lock", LOCK, "read_lock", 0, ret_any},
 	{"__read_unlock", UNLOCK, "read_lock", 0, ret_any},

 	{"__read_lock_irqsave", LOCK, "read_lock", 0, ret_any},
 	{"__read_unlock_irqrestore", UNLOCK, "read_lock", 0, ret_any},
 	{"__read_lock_irq", LOCK, "read_lock", 0, ret_any},
 	{"__read_unlock_irq", UNLOCK, "read_lock", 0, ret_any},

 	{"__read_lock_bh", LOCK, "read_lock", 0, ret_any},
 	{"__read_unlock_bh", UNLOCK, "read_lock", 0, ret_any},
 	{"__write_trylock", LOCK, "write_lock", 0, ret_non_zero},
 	{"__raw_write_trylock", LOCK, "write_lock", 0, ret_non_zero},
 	{"__raw_write_trylock", LOCK, "write_lock", 0, ret_non_zero},
 	{"__write_lock", LOCK, "write_lock", 0, ret_any},
 	{"__write_unlock", UNLOCK, "write_lock", 0, ret_any},

 	{"__write_lock_irqsave", LOCK, "write_lock", 0, ret_any},
 	{"__write_unlock_irqrestore", UNLOCK, "write_lock", 0, ret_any},
 	{"__write_lock_irq", LOCK, "write_lock", 0, ret_any},
 	{"__write_unlock_irq", UNLOCK, "write_lock", 0, ret_any},

 	{"__write_lock_bh", LOCK, "write_lock", 0, ret_any},
 	{"__write_unlock_bh", UNLOCK, "write_lock", 0, ret_any},

	{"lock_kernel", LOCK, "BKL", NO_ARG, ret_any},
	{"unlock_kernel", UNLOCK, "BKL", NO_ARG, ret_any},
	{"_raw_spin_lock", LOCK, "spin_lock", 0, ret_any},
	{"_raw_spin_unlock", UNLOCK, "spin_lock", 0, ret_any},
	{"_raw_spin_trylock", LOCK, "spin_lock", 0, ret_non_zero},
	{"_spin_lock_nested", LOCK, "spin_lock", 0, ret_any},
	{"_spin_trylock", LOCK, "spin_lock", 0, ret_non_zero},
	{"_spin_lock", LOCK, "spin_lock", 0, ret_any},
	{"_spin_unlock", UNLOCK, "spin_lock", 0, ret_any},

 	{"_spin_lock_irqsave_nested", LOCK, "spin_lock", 0, ret_any},
 	{"_spin_lock_irqsave", LOCK, "spin_lock", 0, ret_any},
 	{"_spin_unlock_irqrestore", UNLOCK, "spin_lock", 0, ret_any},
 	{"_spin_lock_irq", LOCK, "spin_lock", 0, ret_any},
 	{"_spin_unlock_irq", UNLOCK, "spin_lock", 0, ret_any},

 	{"_spin_trylock_bh", LOCK, "spin_lock", 0, ret_non_zero},
 	{"_spin_lock_bh", LOCK, "spin_lock", 0, ret_any},
 	{"_spin_unlock_bh", UNLOCK, "spin_lock", 0, ret_any},
 	{"generic__raw_read_trylock", LOCK, "read_lock", 0, ret_non_zero},
 	{"_raw_read_trylock", LOCK, "read_lock", 0, ret_non_zero},
 	{"_read_trylock", LOCK, "read_lock", 0, ret_non_zero},
 	{"_read_lock", LOCK, "read_lock", 0, ret_any},
 	{"_read_unlock", UNLOCK, "read_lock", 0, ret_any},

 	{"_read_lock_irqsave", LOCK, "read_lock", 0, ret_any},
 	{"_read_unlock_irqrestore", UNLOCK, "read_lock", 0, ret_any},
 	{"_read_lock_irq", LOCK, "read_lock", 0, ret_any},
 	{"_read_unlock_irq", UNLOCK, "read_lock", 0, ret_any},

 	{"_read_lock_bh", LOCK, "read_lock", 0, ret_any},
 	{"_read_unlock_bh", UNLOCK, "read_lock", 0, ret_any},
 	{"_write_trylock", LOCK, "write_lock", 0, ret_non_zero},
 	{"_raw_write_trylock", LOCK, "write_lock", 0, ret_non_zero},
 	{"_write_lock", LOCK, "write_lock", 0, ret_any},
 	{"_write_unlock", UNLOCK, "write_lock", 0, ret_any},

 	{"_write_lock_irqsave", LOCK, "write_lock", 0, ret_any},
 	{"_write_unlock_irqrestore", UNLOCK, "write_lock", 0, ret_any},
 	{"_write_lock_irq", LOCK, "write_lock", 0, ret_any},
 	{"_write_unlock_irq", UNLOCK, "write_lock", 0, ret_any},

 	{"_write_lock_bh", LOCK, "write_lock", 0, ret_any},
 	{"_write_unlock_bh", UNLOCK, "write_lock", 0, ret_any},
 	{"down_trylock", LOCK, "sem", 0, ret_zero},
 	{"down_interruptible", LOCK, "sem", 0, ret_zero},
 	{"down", LOCK, "sem", 0, ret_any},
 	{"up", UNLOCK, "sem", 0, ret_any},
 	{"mutex_trylock", LOCK, "mutex", 0, ret_non_zero},
 	{"mutex_lock_interruptible", LOCK, "mutex", 0, ret_zero},
 	{"mutex_lock_interruptible_nested", LOCK, "mutex", 0, ret_zero},
 	{"mutex_lock_killable", LOCK, "mutex", 0, ret_zero},
 	{"mutex_lock_killable_nested", LOCK, "mutex", 0, ret_zero},
 	{"mutex_lock", LOCK, "mutex", 0, ret_any},
 	{"mutex_lock_nested", LOCK, "mutex", 0, ret_any},
 	{"mutex_unlock", UNLOCK, "mutex", 0, ret_any},

	{"raw_local_irq_disable", LOCK, "irq", NO_ARG, ret_any},
	{"__raw_local_irq_save", LOCK, "irqsave", RETURN_VAL, ret_any},
	{"raw_local_irq_restore", UNLOCK, "irqsave", 0, ret_any},

 	{"__spin_lock_irqsave_nested", LOCK, "irqsave", 1, ret_any},
 	{"__spin_lock_irqsave", LOCK, "irqsave", 1, ret_any},
 	{"__spin_unlock_irqrestore", UNLOCK, "irqsave", 1, ret_any},
 	{"__spin_lock_irq", LOCK, "irq", NO_ARG, ret_any},
 	{"__spin_unlock_irq", UNLOCK, "irq", NO_ARG, ret_any},

 	{"__read_lock_irqsave", LOCK, "irqsave", RETURN_VAL, ret_any},
 	{"__read_unlock_irqrestore", UNLOCK, "irqsave", 1, ret_any},
 	{"__read_lock_irq", LOCK, "irq", NO_ARG, ret_any},
 	{"__read_unlock_irq", UNLOCK, "irq", NO_ARG, ret_any},

 	{"__write_lock_irqsave", LOCK, "irqsave", RETURN_VAL, ret_any},
 	{"__write_unlock_irqrestore", UNLOCK, "irqsave", 1, ret_any},
 	{"__write_lock_irq", LOCK, "irq", NO_ARG, ret_any},
 	{"__write_unlock_irq", UNLOCK, "irq", NO_ARG, ret_any},

 	{"_spin_lock_irqsave_nested", LOCK, "irqsave", RETURN_VAL, ret_any},
 	{"_spin_lock_irqsave", LOCK, "irqsave", RETURN_VAL, ret_any},
 	{"_spin_lock_irqsave", LOCK, "irqsave", 1, ret_any},
 	{"_spin_unlock_irqrestore", UNLOCK, "irqsave", 1, ret_any},
 	{"_spin_lock_irq", LOCK, "irq", NO_ARG, ret_any},
 	{"_spin_unlock_irq", UNLOCK, "irq", NO_ARG, ret_any},

 	{"_read_lock_irqsave", LOCK, "irqsave", RETURN_VAL, ret_any},
 	{"_read_lock_irqsave", LOCK, "irqsave", 1, ret_any},
 	{"_read_unlock_irqrestore", UNLOCK, "irqsave", 1, ret_any},
 	{"_read_lock_irq", LOCK, "irq", NO_ARG, ret_any},
 	{"_read_unlock_irq", UNLOCK, "irq", NO_ARG, ret_any},

 	{"_write_lock_irqsave", LOCK, "irqsave", RETURN_VAL, ret_any},
 	{"_write_lock_irqsave", LOCK, "irqsave", 1, ret_any},
 	{"_write_unlock_irqrestore", UNLOCK, "irqsave", 1, ret_any},
 	{"_write_lock_irq", LOCK, "irq", NO_ARG, ret_any},
 	{"_write_unlock_irq", UNLOCK, "irq", NO_ARG, ret_any},

 	{"__spin_trylock_bh", LOCK, "bottom_half", NO_ARG, ret_non_zero},
 	{"__spin_lock_bh", LOCK, "bottom_half", NO_ARG, ret_any},
 	{"__spin_unlock_bh", UNLOCK, "bottom_half", NO_ARG, ret_any},
 	{"__read_lock_bh", LOCK, "bottom_half", NO_ARG, ret_any},
 	{"__read_unlock_bh", UNLOCK, "bottom_half", NO_ARG, ret_any},
 	{"__write_lock_bh", LOCK, "bottom_half", NO_ARG, ret_any},
 	{"__write_unlock_bh", UNLOCK, "bottom_half", NO_ARG, ret_any},
 	{"_spin_trylock_bh", LOCK, "bottom_half", NO_ARG, ret_non_zero},
 	{"_spin_lock_bh", LOCK, "bottom_half", NO_ARG, ret_any},
 	{"_spin_unlock_bh", UNLOCK, "bottom_half", NO_ARG, ret_any},
 	{"_read_lock_bh", LOCK, "bottom_half", NO_ARG, ret_any},
 	{"_read_unlock_bh", UNLOCK, "bottom_half", NO_ARG, ret_any},
 	{"_write_lock_bh", LOCK, "bottom_half", NO_ARG, ret_any},
 	{"_write_unlock_bh", UNLOCK, "bottom_half", NO_ARG, ret_any},
};

static struct lock_info *lock_table;

static struct tracker_list *starts_locked;
static struct tracker_list *starts_unlocked;

struct locks_on_return {
	int line;
	struct tracker_list *locked;
	struct tracker_list *unlocked;
};
DECLARE_PTR_LIST(return_list, struct locks_on_return);
static struct return_list *all_returns;

static char *make_full_name(const char *lock, const char *var)
{
	static char tmp_buf[512];

	snprintf(tmp_buf, 512, "%s:%s", lock, var);
	tmp_buf[511] = '\0';
	return alloc_string(tmp_buf);
}

static char *get_full_name(struct expression *expr, int index)
{
	struct expression *arg;
	char *name = NULL;
	char *full_name = NULL;
	struct lock_info *lock = &lock_table[index];

	if (lock->arg == RETURN_VAL) {
		name = get_variable_from_expr(expr->left, NULL);
		full_name = make_full_name(lock->name, name);
	} else if (lock->arg == NO_ARG) {
		full_name = make_full_name(lock->name, "");
	} else {
		arg = get_argument_from_call_expr(expr->args, lock->arg);
		if (!arg)
			goto free;
		name = get_variable_from_expr(arg, NULL);
		if (!name)
			goto free;
		full_name = make_full_name(lock->name, name);
	}
free:
	free_string(name);
	return full_name;
}

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

static void do_lock(const char *name)
{
	struct sm_state *sm;

	sm = get_sm_state(my_id, name, NULL);
	if (!sm)
		add_tracker(&starts_unlocked, my_id, name, NULL);
	if (sm && slist_has_state(sm->possible, &locked))
		sm_msg("error: double lock '%s'", name);
	set_state(my_id, name, NULL, &locked);
}

static void do_lock_failed(const char *name)
{
	struct sm_state *sm;

	sm = get_sm_state(my_id, name, NULL);
	if (!sm)
		add_tracker(&starts_unlocked, my_id, name, NULL);
	set_state(my_id, name, NULL, &unlocked);
}

static void do_unlock(const char *name)
{
	struct sm_state *sm;

	sm = get_sm_state(my_id, name, NULL);
	if (!sm)
		add_tracker(&starts_locked, my_id, name, NULL);
	if (sm && slist_has_state(sm->possible, &unlocked))
		sm_msg("error: double unlock '%s'", name);
	set_state(my_id, name, NULL, &unlocked);

}

static void match_lock_held(const char *fn, struct expression *call_expr,
			struct expression *assign_expr, void *_index)
{
	int index = (int)_index;
	char *lock_name;
	struct lock_info *lock = &lock_table[index];

	if (lock->arg == NO_ARG) {
		lock_name = get_full_name(NULL, index);
	} else if (lock->arg == RETURN_VAL) {
		if (!assign_expr)
			return;
		lock_name = get_full_name(assign_expr, index);
	} else {
		lock_name = get_full_name(call_expr, index);
	}
	if (!lock_name)
		return;
	do_lock(lock_name);
	free_string(lock_name);
}

static void match_lock_failed(const char *fn, struct expression *call_expr,
			struct expression *assign_expr, void *_index)
{
	int index = (int)_index;
	char *lock_name;
	struct lock_info *lock = &lock_table[index];

	if (lock->arg == NO_ARG) {
		lock_name = get_full_name(NULL, index);
	} else if (lock->arg == RETURN_VAL) {
		if (!assign_expr)
			return;
		lock_name = get_full_name(assign_expr, index);
	} else {
		lock_name = get_full_name(call_expr, index);
	}
	if (!lock_name)
		return;
	do_lock_failed(lock_name);
	free_string(lock_name);
}

static void match_returns_locked(const char *fn, struct expression *expr,
				      void *_index)
{
	char *full_name = NULL;
	int index = (int)_index;
	struct lock_info *lock = &lock_table[index];

	if (lock->arg != RETURN_VAL)
		return;
	full_name = get_full_name(expr, index);
	do_lock(full_name);
}

static void match_lock_unlock(const char *fn, struct expression *expr, void *_index)
{
	char *full_name = NULL;
	int index = (int)_index;
	struct lock_info *lock = &lock_table[index];

	full_name = get_full_name(expr, index);
	if (!full_name)
		return;
	if (lock->action == LOCK)
		do_lock(full_name);
	else
		do_unlock(full_name);
	free_string(full_name);
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

static void register_lock(int index)
{
	struct lock_info *lock = &lock_table[index];
	void *idx = (void *)index;

	if (lock->return_type == ret_non_zero) {
		return_implies_state(lock->function, 1, POINTER_MAX, &match_lock_held, idx);
		return_implies_state(lock->function, 0, 0, &match_lock_failed, idx);
       	} else if (lock->return_type == ret_any && lock->arg == RETURN_VAL) {
		add_function_assign_hook(lock->function, &match_returns_locked, idx);
	} else if (lock->return_type == ret_any) {
		add_function_hook(lock->function, &match_lock_unlock, idx);
	} else if (lock->return_type == ret_zero) {
		return_implies_state(lock->function, 0, 0, &match_lock_held, idx);
		return_implies_state(lock->function, whole_range.min, -1, &match_lock_failed, idx);
	} 
}

static void load_table(struct lock_info *_lock_table, int size)
{
	int i;

	lock_table = _lock_table;

	for (i = 0; i < size; i++) {
		if (lock_table[i].action == LOCK)
			register_lock(i);
		else
			add_function_hook(lock_table[i].function, &match_lock_unlock, (void *)i);
	}
}

void check_wine_locking(int id)
{
	my_id = id;

	if (option_project == PROJ_WINE)
		load_table(wine_lock_table, ARRAY_SIZE(wine_lock_table));
	else if (option_project == PROJ_KERNEL && option_spammy)
		load_table(kernel_lock_table, ARRAY_SIZE(kernel_lock_table));
	else
		return;

	add_unmatched_state_hook(my_id, &unmatched_state);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_func_end, END_FUNC_HOOK);
}
