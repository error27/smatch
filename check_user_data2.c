/*
 * Copyright (C) 2011 Dan Carpenter.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

/*
 * There are a couple checks that try to see if a variable
 * comes from the user.  It would be better to unify them
 * into one place.  Also it we should follow the data down
 * the call paths.  Hence this file.
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;
static int my_call_id;

STATE(called);

static const char * kstr_funcs[] = {
	"kstrtoull", "kstrtoll", "kstrtoul", "kstrtol", "kstrtouint",
	"kstrtoint", "kstrtou64", "kstrtos64", "kstrtou32", "kstrtos32",
	"kstrtou16", "kstrtos16", "kstrtou8", "kstrtos8", "kstrtoull_from_user"
	"kstrtoll_from_user", "kstrtoul_from_user", "kstrtol_from_user",
	"kstrtouint_from_user", "kstrtoint_from_user", "kstrtou16_from_user",
	"kstrtos16_from_user", "kstrtou8_from_user", "kstrtos8_from_user",
	"kstrtou64_from_user", "kstrtos64_from_user", "kstrtou32_from_user",
	"kstrtos32_from_user",
};

static void set_points_to_user_data(struct expression *expr);

static struct stree *start_states;
static struct stree_stack *saved_stack;
static void save_start_states(struct statement *stmt)
{
	start_states = clone_stree(__get_cur_stree());
}

static void free_start_states(void)
{
	free_stree(&start_states);
}

static void match_save_states(struct expression *expr)
{
	push_stree(&saved_stack, start_states);
	start_states = NULL;
}

static void match_restore_states(struct expression *expr)
{
	free_stree(&start_states);
	start_states = pop_stree(&saved_stack);
}

static struct smatch_state *empty_state(struct sm_state *sm)
{
	return alloc_estate_empty();
}

static void pre_merge_hook(struct sm_state *sm)
{
	struct smatch_state *user;
	struct smatch_state *extra;
	struct range_list *rl;

	extra = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (!extra || !estate_rl(extra))
		return;
	user = get_state(my_id, sm->name, sm->sym);
	if (!user || !estate_rl(user))
		return;
	rl = rl_intersection(estate_rl(user), estate_rl(extra));
	set_state(my_id, sm->name, sm->sym, alloc_estate_rl(clone_rl(rl)));
}

static void tag_inner_struct_members(struct expression *expr, struct symbol *member)
{
	struct expression *edge_member;
	struct symbol *base = get_real_base_type(member);
	struct symbol *tmp;

	if (member->ident)
		expr = member_expression(expr, '.', member->ident);

	FOR_EACH_PTR(base->symbol_list, tmp) {
		struct symbol *type;

		type = get_real_base_type(tmp);
		if (!type)
			continue;

		if (type->type == SYM_UNION || type->type == SYM_STRUCT) {
			tag_inner_struct_members(expr, tmp);
			continue;
		}

		if (!tmp->ident)
			continue;

		edge_member = member_expression(expr, '.', tmp->ident);
		set_state_expr(my_id, edge_member, alloc_estate_whole(type));
	} END_FOR_EACH_PTR(tmp);
}

static void tag_struct_members(struct symbol *type, struct expression *expr)
{
	struct symbol *tmp;
	struct expression *member;
	int op = '*';

	if (expr->type == EXPR_PREOP && expr->op == '&') {
		expr = strip_expr(expr->unop);
		op = '.';
	}

	FOR_EACH_PTR(type->symbol_list, tmp) {
		type = get_real_base_type(tmp);
		if (!type)
			continue;

		if (type->type == SYM_UNION || type->type == SYM_STRUCT) {
			tag_inner_struct_members(expr, tmp);
			continue;
		}

		if (!tmp->ident)
			continue;

		member = member_expression(expr, op, tmp->ident);
		set_state_expr(my_id, member, alloc_estate_whole(get_type(member)));

		if (type->type == SYM_ARRAY)
			set_points_to_user_data(member);
	} END_FOR_EACH_PTR(tmp);
}

static void tag_base_type(struct expression *expr)
{
	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = strip_expr(expr->unop);
	else
		expr = deref_expression(expr);
	set_state_expr(my_id, expr, alloc_estate_whole(get_type(expr)));
}

static void tag_as_user_data(struct expression *expr)
{
	struct symbol *type;

	expr = strip_expr(expr);

	type = get_type(expr);
	if (!type || type->type != SYM_PTR)
		return;
	type = get_real_base_type(type);
	if (!type)
		return;
	if (type == &void_ctype) {
		set_state_expr(my_id, deref_expression(expr), alloc_estate_whole(&ulong_ctype));
		return;
	}
	if (type->type == SYM_BASETYPE)
		tag_base_type(expr);
	if (type->type == SYM_STRUCT) {
		if (expr->type != EXPR_PREOP || expr->op != '&')
			expr = deref_expression(expr);
		else
			set_state_expr(my_id, deref_expression(expr), alloc_estate_whole(&ulong_ctype));
		tag_struct_members(type, expr);
	}
}

static void match_user_copy(const char *fn, struct expression *expr, void *_param)
{
	int param = PTR_INT(_param);
	struct expression *dest;

	dest = get_argument_from_call_expr(expr->args, param);
	dest = strip_expr(dest);
	if (!dest)
		return;
	tag_as_user_data(dest);
}

static void match_sscanf(const char *fn, struct expression *expr, void *unused)
{
	struct expression *arg;
	int i;

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		i++;
		if (i < 2)
			continue;
		tag_as_user_data(arg);
	} END_FOR_EACH_PTR(arg);
}

static int points_to_user_data(struct expression *expr)
{
	struct smatch_state *state;
	char buf[256];
	struct symbol *sym;
	char *name;
	int ret = 0;

	expr = strip_expr(expr);

	if (expr->type == EXPR_BINOP && expr->op == '+') {
		if (points_to_user_data(expr->left))
			return 1;
		if (points_to_user_data(expr->right))
			return 1;
		return 0;
	}

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	snprintf(buf, sizeof(buf), "*%s", name);
	state = get_state(my_id, buf, sym);
	if (state && estate_rl(state))
		ret = 1;
free:
	free_string(name);
	return ret;
}

static void set_points_to_user_data(struct expression *expr)
{
	char *name;
	struct symbol *sym;
	char buf[256];

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	snprintf(buf, sizeof(buf), "*%s", name);
	set_state(my_id, buf, sym, alloc_estate_whole(&llong_ctype));
free:
	free_string(name);
}

static int is_skb_data(struct expression *expr)
{
	struct symbol *sym;

	expr = strip_expr(expr);
	if (!expr || expr->type != EXPR_PREOP || expr->op != '*')
		return 0;

	expr = strip_expr(expr->unop);
	if (expr->type == EXPR_BINOP && expr->op == '+')
		expr = strip_expr(expr->left);

	if (expr->type != EXPR_DEREF)
		return 0;

	if (!expr->member)
		return 0;
	if (strcmp(expr->member->name, "data") != 0)
		return 0;

	sym = expr_to_sym(expr->deref);
	if (!sym)
		return 0;
	sym = get_real_base_type(sym);
	if (!sym || sym->type != SYM_PTR)
		return 0;
	sym = get_real_base_type(sym);
	if (!sym || sym->type != SYM_STRUCT || !sym->ident)
		return 0;
	if (strcmp(sym->ident->name, "sk_buff") != 0)
		return 0;

	return 1;
}

static int comes_from_skb_data(struct expression *expr)
{
	expr = strip_expr(expr);
	if (!expr)
		return 0;

	switch (expr->type) {
	case EXPR_BINOP:
		if (comes_from_skb_data(expr->left))
			return 1;
		if (comes_from_skb_data(expr->right))
			return 1;
		return 0;
	case EXPR_PREOP:
		if (is_skb_data(expr))
			return 1;
		return comes_from_skb_data(expr->deref);
	default:
		return 0;
	}
}


static int handle_struct_assignment(struct expression *expr)
{
	struct expression *right;
	struct symbol *left_type, *right_type;

	left_type = get_type(expr->left);
	if (!left_type || left_type->type != SYM_PTR)
		return 0;
	left_type = get_real_base_type(left_type);
	if (!left_type || left_type->type != SYM_STRUCT)
		return 0;

	/*
	 * Ignore struct to struct assignments because for those we look at the
	 * individual members.
	 */
	right = strip_expr(expr->right);
	right_type = get_type(right);
	if (!right_type || right_type->type != SYM_PTR)
		return 0;

	/* If we are assigning struct members then normally that is handled
	 * by fake assignments, however if we cast one struct to a different
	 * of struct then we handle that here.
	 */
	right_type = get_real_base_type(right_type);
	if (right_type == left_type)
		return 0;

	if (!points_to_user_data(right) && !is_skb_data(right))
		return 0;

	tag_as_user_data(expr->left);
	return 1;
}

static int handle_get_user(struct expression *expr)
{
	char *name;
	int ret = 0;

	name = get_macro_name(expr->pos);
	if (!name || strcmp(name, "get_user") != 0)
		return 0;

	name = expr_to_var(expr->right);
	if (!name || strcmp(name, "__val_gu") != 0)
		goto free;
	set_state_expr(my_id, expr->left, alloc_estate_whole(get_type(expr->left)));
	ret = 1;
free:
	free_string(name);
	return ret;
}

static void match_assign(struct expression *expr)
{
	struct range_list *rl;

	if (is_fake_call(expr->right))
		return;
	if (handle_get_user(expr))
		return;
	if (points_to_user_data(expr->right))
		set_points_to_user_data(expr->left);
	if (handle_struct_assignment(expr))
		return;

	if (expr->right->type == EXPR_CALL ||
	    !get_user_rl(expr->right, &rl))
		goto clear_old_state;

	rl = cast_rl(get_type(expr->left), rl);
	set_state_expr(my_id, expr->left, alloc_estate_rl(rl));

	return;

clear_old_state:
	if (get_state_expr(my_id, expr->left))
		set_state_expr(my_id, expr->left, alloc_estate_empty());
}

static void match_user_assign_function(const char *fn, struct expression *expr, void *unused)
{
	tag_as_user_data(expr->left);
	set_points_to_user_data(expr->left);
}

static void match_simple_strtoul(const char *fn, struct expression *expr, void *unused)
{
	set_state_expr(my_id, expr->left, alloc_estate_whole(get_type(expr->left)));
}

static int get_user_macro_rl(struct expression *expr, struct range_list **rl)
{
	char *macro;

	if (!expr)
		return 0;
	macro = get_macro_name(expr->pos);

	if (!macro)
		return 0;

	if (strcmp(macro, "ntohl") == 0) {
		*rl = alloc_whole_rl(&uint_ctype);
		return 1;
	}
	if (strcmp(macro, "ntohs") == 0) {
		*rl = alloc_whole_rl(&ushort_ctype);
		return 1;
	}
	return 0;
}

static int user_data_flag;
static struct range_list *var_user_rl(struct expression *expr)
{
	struct smatch_state *state;
	struct range_list *rl;
	struct range_list *absolute_rl;

	if (get_user_macro_rl(expr, &rl))
		goto found;

	if (comes_from_skb_data(expr)) {
		rl = alloc_whole_rl(get_type(expr));
		goto found;
	}

	state = get_state_expr(my_id, expr);
	if (state && estate_rl(state)) {
		rl = estate_rl(state);
		goto found;
	}

	return NULL;
found:
	user_data_flag = 1;
	absolute_rl = var_to_absolute_rl(expr);
	return clone_rl(rl_intersection(rl, absolute_rl));
}

int get_user_rl(struct expression *expr, struct range_list **rl)
{

	user_data_flag = 0;
	custom_get_absolute_rl(expr, &var_user_rl, rl);
	if (!user_data_flag) {
		*rl = NULL;
		return 0;
	}
	return 1;
}

int get_user_rl_var_sym(const char *name, struct symbol *sym, struct range_list **rl)
{
	struct smatch_state *state;

	state = get_state(my_id, name, sym);
	if (state && estate_rl(state)) {
		*rl = estate_rl(state);
		return 1;
	}
	return 0;
}

static void match_call_info(struct expression *expr)
{
	struct range_list *rl;
	struct expression *arg;
	int i = 0;

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		i++;

		if (!get_user_rl(arg, &rl))
			continue;

		sql_insert_caller_info(expr, USER_DATA3, i, "$", show_rl(rl));
	} END_FOR_EACH_PTR(arg);
}

static void struct_member_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	struct smatch_state *state;
	struct range_list *rl;

	if (strcmp(sm->state->name, "") == 0)
		return;

	state = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (!state || !estate_rl(state))
		rl = estate_rl(sm->state);
	else
		rl = rl_intersection(estate_rl(sm->state), estate_rl(state));

	sql_insert_caller_info(call, USER_DATA3, param, printed_name, show_rl(rl));
}

static void set_param_user_data(const char *name, struct symbol *sym, char *key, char *value)
{
	struct range_list *rl = NULL;
	struct smatch_state *state;
	struct symbol *type;
	char fullname[256];

	if (strcmp(key, "*$") == 0)
		snprintf(fullname, sizeof(fullname), "*%s", name);
	else if (strncmp(key, "$", 1) == 0)
		snprintf(fullname, 256, "%s%s", name, key + 1);
	else
		return;

	type = get_member_type_from_key(symbol_expression(sym), key);

	/* if the caller passes a void pointer with user data */
	if (strcmp(key, "*$") == 0 && type && type != &void_ctype) {
		struct expression *expr = symbol_expression(sym);

		tag_as_user_data(expr);
		set_points_to_user_data(expr);
		return;
	}
	str_to_rl(type, value, &rl);
	state = alloc_estate_rl(rl);
	set_state(my_id, fullname, sym, state);
}

static void set_called(const char *name, struct symbol *sym, char *key, char *value)
{
	set_state(my_call_id, "this_function", NULL, &called);
}

static void match_syscall_definition(struct symbol *sym)
{
	struct symbol *arg;
	char *macro;
	char *name;
	int is_syscall = 0;

	macro = get_macro_name(sym->pos);
	if (macro &&
	    (strncmp("SYSCALL_DEFINE", macro, strlen("SYSCALL_DEFINE")) == 0 ||
	     strncmp("COMPAT_SYSCALL_DEFINE", macro, strlen("COMPAT_SYSCALL_DEFINE")) == 0))
		is_syscall = 1;

	name = get_function();
	if (!option_no_db && get_state(my_call_id, "this_function", NULL) != &called) {
		if (name && strncmp(name, "sys_", 4) == 0)
			is_syscall = 1;
	}

	if (name && strncmp(name, "compat_sys_", 11) == 0)
		is_syscall = 1;

	if (!is_syscall)
		return;

	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		set_state(my_id, arg->ident->name, arg, alloc_estate_whole(get_real_base_type(arg)));
	} END_FOR_EACH_PTR(arg);
}

static void returns_param_user_data(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;
	char *name;
	struct symbol *sym;
	struct symbol *type;
	struct range_list *rl = NULL;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;
	type = get_member_type_from_key(arg, key);
	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	call_results_to_rl(expr, type, value, &rl);

	set_state(my_id, name, sym, alloc_estate_rl(rl));
free:
	free_string(name);
}

static int has_empty_state(struct sm_state *sm)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (!estate_rl(tmp->state))
			return 1;
	} END_FOR_EACH_PTR(tmp);

	return 0;
}

static void param_set_to_user_data(int return_id, char *return_ranges, struct expression *expr)
{
	struct sm_state *sm;
	struct smatch_state *start_state;
	int param;
	const char *param_name;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (has_empty_state(sm))
			continue;

		param = get_param_num_from_sym(sm->sym);
		if (param < 0) {
			if (expr_to_sym(expr) == sm->sym)
				param = -1;
			else
				continue;
		}

		/* The logic here was that if we were passed in a user data then
		 * we don't record that.  It's like the difference between
		 * param_filter and param_set.  When I think about it, I'm not
		 * sure it actually works.  It's probably harmless because we
		 * checked earlier that we're not returning a parameter...
		 * Let's mark this as a TODO.
		 */
		start_state = get_state_stree(start_states, my_id, sm->name, sm->sym);
		if (start_state && estates_equiv(sm->state, start_state))
			continue;

		param_name = get_param_name(sm);
		if (!param_name)
			continue;
		if (strcmp(param_name, "$") == 0)
			continue;

		sql_insert_return_states(return_id, return_ranges, USER_DATA3,
					 param, param_name, show_rl(estate_rl(sm->state)));
	} END_FOR_EACH_SM(sm);
}

void check_user_data2(int id)
{
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(&save_start_states, AFTER_DEF_HOOK);
	add_hook(&free_start_states, AFTER_FUNC_HOOK);
	add_hook(&match_save_states, INLINE_FN_START);
	add_hook(&match_restore_states, INLINE_FN_END);

	add_unmatched_state_hook(my_id, &empty_state);
	add_pre_merge_hook(my_id, &pre_merge_hook);
	add_merge_hook(my_id, &merge_estates);

	add_function_hook("copy_from_user", &match_user_copy, INT_PTR(0));
	add_function_hook("__copy_from_user", &match_user_copy, INT_PTR(0));
	add_function_hook("memcpy_fromiovec", &match_user_copy, INT_PTR(0));
	for (i = 0; i < ARRAY_SIZE(kstr_funcs); i++)
		add_function_hook(kstr_funcs[i], &match_user_copy, INT_PTR(2));

	add_function_assign_hook("simple_strtol", &match_simple_strtoul, NULL);
	add_function_assign_hook("simple_strtoll", &match_simple_strtoul, NULL);
	add_function_assign_hook("simple_strtoul", &match_simple_strtoul, NULL);
	add_function_assign_hook("simple_strtoull", &match_simple_strtoul, NULL);

	add_function_hook("sscanf", &match_sscanf, NULL);

	add_function_assign_hook("memdup_user", &match_user_assign_function, NULL);
	add_function_assign_hook("kmap_atomic", &match_user_assign_function, NULL);
	add_function_assign_hook("skb_network_header", &match_user_assign_function, NULL);

	add_hook(&match_syscall_definition, AFTER_DEF_HOOK);

	add_hook(&match_assign, ASSIGNMENT_HOOK);

	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
	add_member_info_callback(my_id, struct_member_callback);
	select_caller_info_hook(set_param_user_data, USER_DATA3);
	select_return_states_hook(USER_DATA3, &returns_param_user_data);
	add_split_return_callback(&param_set_to_user_data);
}

void check_user_data3(int id)
{
	my_call_id = id;

	if (option_project != PROJ_KERNEL)
		return;
	select_caller_info_hook(set_called, INTERNAL);
}

