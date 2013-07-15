/*
 * smatch/check_user_data.c
 *
 * Copyright (C) 2011 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
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

void tag_as_user_data(struct expression *expr);

static int my_id;

STATE(capped);
STATE(user_data_passed);
STATE(user_data_set);

enum {
	SET_DATA = 1,
	PASSED_DATA = 2,
};

static int is_user_macro(struct expression *expr)
{
	char *macro;
	struct range_list *rl;

	macro = get_macro_name(expr->pos);

	if (!macro)
		return 0;
	if (get_implied_rl(expr, &rl) && !is_whole_rl(rl))
		return 0;
	if (strcmp(macro, "ntohl") == 0)
		return SET_DATA;
	if (strcmp(macro, "ntohs") == 0)
		return SET_DATA;
	return 0;
}

static int has_user_data_state(struct expression *expr, struct state_list *my_slist)
{
	struct sm_state *sm;
	struct symbol *sym;
	char *name;

	expr = strip_expr(expr);
	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = strip_expr(expr->unop);

	name = expr_to_str_sym(expr, &sym);
	free_string(name);
	if (!sym)
		return 1;

	FOR_EACH_PTR(my_slist, sm) {
		if (sm->sym == sym)
			return 1;
	} END_FOR_EACH_PTR(sm);
	return 0;
}

static int passes_user_data(struct expression *expr)
{
	struct state_list *slist;
	struct expression *arg;

	slist = get_all_states(my_id);
	FOR_EACH_PTR(expr->args, arg) {
		if (is_user_data(arg))
			return 1;
		if (has_user_data_state(arg, slist))
			return 1;
	} END_FOR_EACH_PTR(arg);

	return 0;
}

static struct expression *db_expr;
static int db_user_data;
static int db_user_data_callback(void *unused, int argc, char **argv, char **azColName)
{
	if (atoi(argv[0]) == PASSED_DATA && !passes_user_data(db_expr))
		return 0;
	db_user_data = 1;
	return 0;
}

static int is_user_fn_db(struct expression *expr)
{
	struct symbol *sym;
	static char sql_filter[1024];

	if (expr->fn->type != EXPR_SYMBOL)
		return 0;
	sym = expr->fn->symbol;
	if (!sym)
		return 0;

	if (sym->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, 1024, "file = '%s' and function = '%s';",
			 get_filename(), sym->ident->name);
	} else {
		snprintf(sql_filter, 1024, "function = '%s' and static = 0;",
				sym->ident->name);
	}

	db_expr = expr;
	db_user_data = 0;
	run_sql(db_user_data_callback,
		"select value from return_states where type=%d and parameter = -1 and key = '$$' and %s",
		USER_DATA, sql_filter);
	return db_user_data;
}

static int is_user_function(struct expression *expr)
{
	if (expr->type != EXPR_CALL)
		return 0;
	if (sym_name_is("kmemdup_user", expr->fn))
		return SET_DATA;
	return is_user_fn_db(expr);
}

static int is_skb_data(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	int len;
	int ret = 0;

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	sym = get_base_type(sym);
	if (!sym || sym->type != SYM_PTR)
		goto free;
	sym = get_base_type(sym);
	if (!sym || sym->type != SYM_STRUCT || !sym->ident)
		goto free;
	if (strcmp(sym->ident->name, "sk_buff") != 0)
		goto free;

	len = strlen(name);
	if (len < 6)
		goto free;
	if (strcmp(name + len - 6, "->data") == 0)
		ret = SET_DATA;

free:
	free_string(name);
	return ret;
}

static int in_container_of_macro(struct expression *expr)
{
	char *macro;

	macro = get_macro_name(expr->pos);

	if (!macro)
		return 0;
	if (strcmp(macro, "container_of") == 0)
		return 1;
	return 0;
}

static int is_user_data_state(struct expression *expr)
{
	struct state_list *slist = NULL;
	struct sm_state *tmp;
	struct symbol *sym;
	char *name;
	int user = 0;

	tmp = get_sm_state_expr(my_id, expr);
	if (tmp) {
		if (slist_has_state(tmp->possible, &user_data_set))
			return SET_DATA;
		if (slist_has_state(tmp->possible, &user_data_passed))
			return PASSED_DATA;
		return 0;
	}

	name = expr_to_str_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	slist = get_all_states(my_id);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->sym != sym)
			continue;
		if (!strncmp(tmp->name, name, strlen(tmp->name))) {
			if (slist_has_state(tmp->possible, &user_data_set))
				user = SET_DATA;
			else if (slist_has_state(tmp->possible, &user_data_passed))
				user = PASSED_DATA;
			goto free;
		}
	} END_FOR_EACH_PTR(tmp);

free:
	free_slist(&slist);
	free_string(name);
	return user;
}

int is_user_data(struct expression *expr)
{
	int user_data;

	if (!expr)
		return 0;

	if (is_capped(expr))
		return 0;
	if (in_container_of_macro(expr))
		return 0;

	user_data = is_user_macro(expr);
	if (user_data)
		return user_data;
	user_data = is_user_function(expr);
	if (user_data)
		return user_data;
	user_data = is_skb_data(expr);
	if (user_data)
		return user_data;

	expr = strip_expr(expr);  /* this has to come after is_user_macro() */

	if (expr->type == EXPR_BINOP) {
		user_data = is_user_data(expr->left);
		if (user_data)
			return user_data;
		if (is_array(expr))
			return 0;
		user_data = is_user_data(expr->right);
		if (user_data)
			return user_data;
		return 0;
	}
	if (expr->type == EXPR_PREOP && (expr->op == '&' || expr->op == '*'))
		expr = strip_expr(expr->unop);

	return is_user_data_state(expr);
}

int is_capped_user_data(struct expression *expr)
{
	struct sm_state *sm;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm)
		return 0;
	if (slist_has_state(sm->possible, &capped))
		return 1;
	return 0;
}

void set_param_user_data(const char *name, struct symbol *sym, char *key, char *value)
{
	char fullname[256];

	/* sanity check.  this should always be true. */
	if (strncmp(key, "$$", 2) != 0)
		return;
	snprintf(fullname, 256, "%s%s", name, key + 2);
	set_state(my_id, fullname, sym, &user_data_passed);
}

static void match_syscall_definition(struct symbol *sym)
{
	struct symbol *arg;
	char *macro;

	macro = get_macro_name(sym->pos);
	if (!macro)
		return;
	if (strncmp("SYSCALL_DEFINE", macro, strlen("SYSCALL_DEFINE")) != 0 &&
	    strncmp("COMPAT_SYSCALL_DEFINE", macro, strlen("COMPAT_SYSCALL_DEFINE")) != 0)
		return;

	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		set_state(my_id, arg->ident->name, arg, &user_data_set);
	} END_FOR_EACH_PTR(arg);
}

static void match_condition(struct expression *expr)
{
	switch (expr->op) {
	case '<':
	case SPECIAL_LTE:
	case SPECIAL_UNSIGNED_LT:
	case SPECIAL_UNSIGNED_LTE:
		if (is_user_data(expr->left))
			set_true_false_states_expr(my_id, expr->left, &capped, NULL);
		if (is_user_data(expr->right))
			set_true_false_states_expr(my_id, expr->right, NULL, &capped);
		break;
	case '>':
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GT:
	case SPECIAL_UNSIGNED_GTE:
		if (is_user_data(expr->right))
			set_true_false_states_expr(my_id, expr->right, &capped, NULL);
		if (is_user_data(expr->left))
			set_true_false_states_expr(my_id, expr->left, NULL, &capped);
		break;
	case SPECIAL_EQUAL:
		if (is_user_data(expr->left))
			set_true_false_states_expr(my_id, expr->left, &capped, NULL);
		if (is_user_data(expr->right))
			set_true_false_states_expr(my_id, expr->right, &capped, NULL);
		break;
	case SPECIAL_NOTEQUAL:
		if (is_user_data(expr->left))
			set_true_false_states_expr(my_id, expr->left, NULL, &capped);
		if (is_user_data(expr->right))
			set_true_false_states_expr(my_id, expr->right, NULL, &capped);
		break;
	default:
		return;
	}
}

static void set_capped(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_id, sm->name, sm->sym, &capped);
}

static void match_normal_assign(struct expression *expr)
{
	int user_data;

	user_data = is_user_data(expr->right);
	if (user_data == PASSED_DATA)
		set_state_expr(my_id, expr->left, &user_data_passed);
	if (user_data == SET_DATA)
		set_state_expr(my_id, expr->left, &user_data_set);
}

static void match_assign(struct expression *expr)
{
	char *name;

	name = get_macro_name(expr->pos);
	if (!name || strcmp(name, "get_user") != 0) {
		match_normal_assign(expr);
		return;
	}
	name = expr_to_var(expr->right);
	if (!name || strcmp(name, "__val_gu") != 0)
		goto free;
	set_state_expr(my_id, expr->left, &user_data_set);
free:
	free_string(name);
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
		if (!tmp->ident)
			continue;
		member = member_expression(expr, op, tmp->ident);
		set_state_expr(my_id, member, &user_data_set);
	} END_FOR_EACH_PTR(tmp);
}

static void tag_base_type(struct expression *expr)
{
	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = strip_expr(expr->unop);
	else
		expr = deref_expression(expr);
	set_state_expr(my_id, expr, &user_data_set);
}

void tag_as_user_data(struct expression *expr)
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
		set_state_expr(my_id, deref_expression(expr), &user_data_set);
		return;
	}
	if (type->type == SYM_BASETYPE)
		tag_base_type(expr);
	if (type->type == SYM_STRUCT) {
		if (expr->type != EXPR_PREOP || expr->op != '&')
			expr = deref_expression(expr);
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

static void match_user_assign_function(const char *fn, struct expression *expr, void *unused)
{
	set_state_expr(my_id, expr->left, &user_data_set);
}

static void match_macro_assign(const char *fn, struct expression *expr, void *_bits)
{
	int bits;

	bits = nr_bits(expr->left);
	if (!bits)
		return;
	if (bits > nr_bits(expr->right))
		return;
	set_state_expr(my_id, expr->left, &user_data_set);
}

static void match_caller_info(struct expression *expr)
{
	struct expression *tmp;
	int i;

	i = 0;
	FOR_EACH_PTR(expr->args, tmp) {
		if (is_user_data(tmp))
			sql_insert_caller_info(expr, USER_DATA, i, "$$", "");
		i++;
	} END_FOR_EACH_PTR(tmp);
}

static void struct_member_callback(struct expression *call, int param, char *printed_name, struct smatch_state *state)
{
	if (state == &capped)
		return;
	sql_insert_caller_info(call, USER_DATA, param, printed_name, "");
}

static void returned_member_callback(int return_id, char *return_ranges, char *printed_name, struct smatch_state *state)
{
	if (state == &capped)
		return;
	sql_insert_return_states(return_id, return_ranges, USER_DATA, -1, printed_name, "");
}

static void print_returned_user_data(int return_id, char *return_ranges, struct expression *expr)
{
	struct state_list *my_slist;
	struct sm_state *tmp;
	int param;
	int user_data;
	const char *passed_or_new;

	user_data = is_user_data(expr);
	if (user_data == PASSED_DATA) {
		sql_insert_return_states(return_id, return_ranges, USER_DATA,
				-1, "$$", "2");
	}
	if (user_data == SET_DATA) {
		sql_insert_return_states(return_id, return_ranges, USER_DATA,
				-1, "$$", "1");
	}

	my_slist = get_all_states(my_id);

	FOR_EACH_PTR(my_slist, tmp) {
		const char *param_name;

		param = get_param_num_from_sym(tmp->sym);
		if (param < 0)
			continue;

		if (is_capped_var_sym(tmp->name, tmp->sym))
			continue;
		/* ignore states that were already USER_DATA to begin with */
		if (get_state_slist(get_start_states(), my_id, tmp->name, tmp->sym))
			continue;

		param_name = get_param_name(tmp);
		if (!param_name)
			return;

		if (slist_has_state(tmp->possible, &user_data_set))
			passed_or_new = "1";
		if (slist_has_state(tmp->possible, &user_data_passed))
			passed_or_new = "2";

		sql_insert_return_states(return_id, return_ranges, USER_DATA,
				param, param_name, passed_or_new);
	} END_FOR_EACH_PTR(tmp);

	free_slist(&my_slist);
}

static void db_return_states_userdata(struct expression *expr, int param, char *key, char *value)
{
	char *name;
	struct symbol *sym;

	name = return_state_to_var_sym(expr, param, key, &sym);
	if (!name || !sym)
		goto free;

	set_state(my_id, name, sym, &user_data_set);
free:
	free_string(name);
}

void check_user_data(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	my_id = id;
	select_caller_info_hook(set_param_user_data, USER_DATA);
	add_hook(&match_syscall_definition, FUNC_DEF_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_function_hook("copy_from_user", &match_user_copy, INT_PTR(0));
	add_function_hook("__copy_from_user", &match_user_copy, INT_PTR(0));
	add_function_hook("memcpy_fromiovec", &match_user_copy, INT_PTR(0));
	add_function_assign_hook("kmemdup_user", &match_user_assign_function, NULL);

	add_hook(&match_caller_info, FUNCTION_CALL_HOOK);
	add_member_info_callback(my_id, struct_member_callback);
	add_returned_member_callback(my_id, returned_member_callback);
	add_returned_state_callback(print_returned_user_data);
	select_return_states_hook(USER_DATA, &db_return_states_userdata);

	add_modification_hook(my_id, &set_capped);

	add_macro_assign_hook("ntohl", &match_macro_assign, NULL);
	add_macro_assign_hook("ntohs", &match_macro_assign, NULL);
}
