/*
 * Copyright (C) 2017 Oracle.
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

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;
static int param_id;

static struct stree *used_stree;
static struct stree_stack *saved_stack;

STATE(used);

int get_param_from_container_of(struct expression *expr)
{
	struct expression *param_expr;
	struct symbol *type;
	sval_t sval;
	int param;


	type = get_type(expr);
	if (!type || type->type != SYM_PTR)
		return -1;

	expr = strip_expr(expr);
	if (expr->type != EXPR_BINOP || expr->op != '-')
		return -1;

	if (!get_value(expr->right, &sval))
		return -1;
	if (sval.value < 0 || sval.value > 4096)
		return -1;

	param_expr = get_assigned_expr(expr->left);
	if (!param_expr)
		return -1;
	param = get_param_num(param_expr);
	if (param < 0)
		return -1;

	return param;
}

int get_offset_from_container_of(struct expression *expr)
{
	struct expression *param_expr;
	struct symbol *type;
	sval_t sval;

	type = get_type(expr);
	if (!type || type->type != SYM_PTR)
		return -1;

	expr = strip_expr(expr);
	if (expr->type != EXPR_BINOP || expr->op != '-')
		return -1;

	if (!get_value(expr->right, &sval))
		return -1;
	if (sval.value < 0 || sval.value > 4096)
		return -1;

	param_expr = get_assigned_expr(expr->left);
	if (!param_expr)
		return -1;

	return sval.value;
}

static int get_container_arg(struct symbol *sym)
{
	struct expression *__mptr;
	int param;

	if (!sym || !sym->ident)
		return -1;

	__mptr = get_assigned_expr_name_sym(sym->ident->name, sym);
	param = get_param_from_container_of(__mptr);

	return param;
}

static int get_container_offset(struct symbol *sym)
{
	struct expression *__mptr;
	int offset;

	if (!sym || !sym->ident)
		return -1;

	__mptr = get_assigned_expr_name_sym(sym->ident->name, sym);
	offset = get_offset_from_container_of(__mptr);

	return offset;
}

static char *get_container_name(struct sm_state *sm, int offset)
{
	static char buf[256];
	const char *name;

	name = get_param_name(sm);
	if (!name)
		return NULL;

	if (name[0] == '$')
		snprintf(buf, sizeof(buf), "$(-%d)%s", offset, name + 1);
	else if (name[0] == '*' || name[1] == '$')
		snprintf(buf, sizeof(buf), "*$(-%d)%s", offset, name + 2);
	else
		return NULL;

	return buf;
}

static void get_state_hook(int owner, const char *name, struct symbol *sym)
{
	int arg;

	if (!option_info)
		return;
	if (__in_fake_assign)
		return;

	arg = get_container_arg(sym);
	if (arg >= 0)
		set_state_stree(&used_stree, my_id, name, sym, &used);
}

static void set_param_used(struct expression *call, struct expression *arg, char *key, char *unused)
{
	struct symbol *sym;
	char *name;
	int arg_nr;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	arg_nr = get_container_arg(sym);
	if (arg_nr >= 0)
		set_state(my_id, name, sym, &used);
free:
	free_string(name);
}

static void process_states(void)
{
	struct sm_state *tmp;
	int arg, offset;
	const char *name;

	FOR_EACH_SM(used_stree, tmp) {
		arg = get_container_arg(tmp->sym);
		offset = get_container_offset(tmp->sym);
		if (arg < 0 || offset < 0)
			continue;
		name = get_container_name(tmp, offset);
		if (!name)
			continue;
		sql_insert_return_implies(CONTAINER, arg, name, "");
	} END_FOR_EACH_SM(tmp);

	free_stree(&used_stree);
}

static void match_function_def(struct symbol *sym)
{
	free_stree(&used_stree);
}

static void match_save_states(struct expression *expr)
{
	push_stree(&saved_stack, used_stree);
	used_stree = NULL;
}

static void match_restore_states(struct expression *expr)
{
	free_stree(&used_stree);
	used_stree = pop_stree(&saved_stack);
}

static void print_returns_container_of(int return_id, char *return_ranges, struct expression *expr)
{
	int offset;
	int param;
	char key[64];
	char value[64];

	param = get_param_from_container_of(expr);
	if (param < 0)
		return;
	offset = get_offset_from_container_of(expr);
	if (offset < 0)
		return;

	snprintf(key, sizeof(key), "%d", param);
	snprintf(value, sizeof(value), "-%d", offset);

	/* no need to add it to return_implies because it's not really param_used */
	sql_insert_return_states(return_id, return_ranges, CONTAINER, -1,
			key, value);
}

static void returns_container_of(struct expression *expr, int param, char *key, char *value)
{
	struct expression *call, *arg;
	int offset;
	char buf[64];

	if (expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return;
	call = strip_expr(expr->right);
	if (call->type != EXPR_CALL)
		return;
	if (param != -1)
		return;
	param = atoi(key);
	offset = atoi(value);

	arg = get_argument_from_call_expr(call->args, param);
	if (!arg)
		return;
	if (arg->type != EXPR_SYMBOL)
		return;
	param = get_param_num(arg);
	if (param < 0)
		return;
	snprintf(buf, sizeof(buf), "$(%d)", offset);
	sql_insert_return_implies(CONTAINER, param, buf, "");
}

static struct expression *get_outer_struct(struct expression *expr)
{
	expr = strip_expr(expr);

	if (expr->type != EXPR_DEREF)
		return NULL;
	expr = strip_expr(expr->deref);
	if (expr->type == EXPR_SYMBOL)
		return expr;
	if (expr->type == EXPR_PREOP && expr->op == '*')
		return strip_expr(expr->unop);
	return expr;
}

static void match_call(struct expression *call)
{
	struct expression *fn_ptr, *fn_parent, *arg, *arg_parent;
	struct symbol *fn_sym, *arg_sym;
	struct symbol *type;
	int i, offset;
	int arg_offset = 0;
	char parent_str[64];
	char offset_str[64];
	char param_str[64];
	char *p;
	bool star = 0;

	/*
	 * We're trying to link the function with the parameter.  There are a
	 * couple ways this can be passed:
	 * foo->func(foo, ...);
	 * foo->func(foo->x, ...);
	 * foo->bar.func(&foo->bar, ...);
	 * foo->bar->baz->func(foo, ...);
	 *
	 * So the method is basically to subtract the offsets until we get to
	 * the common bit, then the member offsets for the parameter.
	 *
	 * If the argument is not a pointer then we star it.  This is perhaps
	 * not right.  We should probably be treating addresses of a pointer
	 * different from the pointer value.
	 *
	 */
	fn_ptr = strip_expr(call->fn);
	fn_parent = get_outer_struct(fn_ptr);
	if (!fn_parent)
		return;
	fn_sym = expr_to_sym(fn_parent);
	if (!fn_sym)
		return;

	type = get_type(fn_parent);
	if (type && type->type == SYM_PTR)
		type = get_real_base_type(type);
	offset = get_member_offset(type, fn_ptr->member->name);
	if (offset < 0)
		return;

	i = -1;
	FOR_EACH_PTR(call->args, arg) {
		i++;

#if 0
		/*
		 * This doesn't work because of things like:
		 * foo = bar;
		 * foo->func(foo, ...);
		 * We don't want to use "bar" here.  But also perhaps I should
		 * be changing the short names to long names or something?
		 *
		 */
		count = 0;
		while ((tmp = get_assigned_expr(arg))) {
			arg = strip_expr(tmp);
			if (count++ > 4)
				break;
		}
#endif
		arg_sym = expr_to_sym(arg);
		if (!arg_sym)
			continue;
		if (arg_sym != fn_sym)
			continue;

		/*
		 * We know these are related because arg_sym == fn_sym.
		 * If we don't record something in the DB, that is a failure.
		 *
		 */

		if (arg->type == EXPR_PREOP && arg->op == '&')
			arg = strip_expr(arg->unop);

		if (expr_equiv(arg, fn_parent)) {
			snprintf(parent_str, sizeof(parent_str), "-%d", offset);
			goto found;
		}

		p = parent_str;
		while ((arg_parent = get_outer_struct(arg))) {
			arg = arg_parent;

			p += snprintf(p, parent_str + sizeof(parent_str) - p, "-%d", offset);

			if (!expr_equiv(arg, fn_parent))
				continue;
			arg_offset = get_member_offset_from_deref(arg);
			if (arg_offset < 0)
				continue;
			if (!is_pointer(arg))
				star = 1;
			goto found;
		}
	} END_FOR_EACH_PTR(arg);

	return;

found:
	if (star)
		snprintf(offset_str, sizeof(offset_str), "*(%s+%d)", parent_str, arg_offset);
	else
		snprintf(offset_str, sizeof(offset_str), "%s+%d", parent_str, arg_offset);
	snprintf(param_str, sizeof(param_str), "$%d", i);
	sql_insert_caller_info(call, CONTAINER, -1, offset_str, param_str);
}

static void db_passed_container(const char *name, struct symbol *sym, char *key, char *value)
{
	sval_t offset = {
		.type = &int_ctype,
	};
	const char *arg_offset;
	struct symbol *arg;
	int star = 0;
	int param;
	int val;
	int i;

	if (name || sym)
		return;
	if (key[0] == '*') {
		star = 1;
		key += 2;
	}

	val = atoi(key);
	if (val < -4095 || val >= 0)
		return;
	offset.value = -val;
	arg_offset = strchr(key, '+');
	if (!arg_offset)
		return;
	val = atoi(arg_offset + 1);
	if (val > 4095 || val < 0)
		return;
	offset.value |= val << 16;
	if (star)
		offset.value |= 1ULL << 31;

	if (value[0] != '$')
		return;

	param = atoi(value + 1);

	i = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		i++;
		if (param == i)
			set_state(param_id, arg->ident->name, arg, alloc_estate_sval(offset));
	} END_FOR_EACH_PTR(arg);
}

struct db_info {
	struct symbol *param_sym;
	int prev_offset;
	struct range_list *rl;
};

static struct symbol *get_member_from_offset(struct symbol *sym, int offset)
{
	struct symbol *type, *tmp;
	int cur;

	type = get_real_base_type(sym);
	if (!type || type->type != SYM_PTR)
		return NULL;
	type = get_real_base_type(type);
	if (!type || type->type != SYM_STRUCT)
		return NULL;

	cur = 0;
	FOR_EACH_PTR(type->symbol_list, tmp) {
		cur = ALIGN(cur, tmp->ctype.alignment);
		if (offset == cur)
			return tmp;
		cur += type_bytes(tmp);
	} END_FOR_EACH_PTR(tmp);
	return NULL;
}

static struct symbol *get_member_type_from_offset(struct symbol *sym, int offset)
{
	struct symbol *member;

	member = get_member_from_offset(sym, offset);
	if (!member)
		return NULL;
	return get_real_base_type(member);
}

static void set_param_value(struct symbol *sym, int offset, struct range_list *rl)
{
	struct symbol *member;
	const char *name;
	char fullname[256];

	if (!sym || !sym->ident)
		return;
	name = sym->ident->name;

	member = get_member_from_offset(sym, offset);
	if (!member) {
		if (offset == 0)
			snprintf(fullname, sizeof(fullname), "%s", name);
		else
			return;
	} else {
		snprintf(fullname, sizeof(fullname), "%s->%s", name, member->ident->name);
	}

	set_state(SMATCH_EXTRA, fullname, sym, alloc_estate_rl(rl));
}

static int save_vals(void *_db_info, int argc, char **argv, char **azColName)
{
	struct db_info *db_info = _db_info;
	struct symbol *type;
	struct range_list *rl;
	int offset = 0;
	const char *value;

	if (argc == 2) {
		offset = atoi(argv[0]);
		value = argv[1];
	} else {
		value = argv[0];
	}

	if (db_info->prev_offset != -1 &&
	    db_info->prev_offset != offset) {
		set_param_value(db_info->param_sym, db_info->prev_offset, db_info->rl);
		db_info->rl = NULL;
	}

	db_info->prev_offset = offset;

	type = get_member_type_from_offset(db_info->param_sym, offset);
	str_to_rl(type, (char *)value, &rl);
	if (db_info->rl)
		db_info->rl = rl_union(db_info->rl, rl);
	else
		db_info->rl = rl;

	return 0;
}

static void load_tag_info_sym(mtag_t tag, struct symbol *sym, int arg_offset, int star)
{
	struct db_info db_info = {
		.param_sym = sym,
		.prev_offset = -1,
	};

	if (!tag)
		return;

	if (star) {
		run_sql(save_vals, &db_info,
			"select value from mtag_data where tag = %lld and offset = %d and type = %d;",
			tag, arg_offset, DATA_VALUE);
	} else {
		run_sql(save_vals, &db_info,
			"select offset, value from mtag_data where tag = %lld and type = %d;",
			tag, DATA_VALUE);
	}

	if (db_info.prev_offset != -1)
		set_param_value(db_info.param_sym, db_info.prev_offset, db_info.rl);
}

static void handle_passed_container(struct symbol *sym)
{
	struct symbol *arg;
	struct smatch_state *state;
	mtag_t fn_tag, container_tag, arg_tag;
	sval_t offset;
	int container_offset, arg_offset;
	int star;

	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		state = get_state(param_id, arg->ident->name, arg);
		if (state)
			goto found;
	} END_FOR_EACH_PTR(arg);

	return;
found:
	if (!estate_get_single_value(state, &offset))
		return;
	container_offset = -(offset.value & 0xffff);
	arg_offset = -((offset.value & 0xfff0000) >> 16);
	star = !!(offset.value & (1ULL << 31));

	if (!get_toplevel_mtag(cur_func_sym, &fn_tag))
		return;
	if (!mtag_map_select_container(fn_tag, container_offset, &container_tag))
		return;
	if (!arg_offset || star) {
		arg_tag = container_tag;
	} else {
		if (!mtag_map_select_tag(container_tag, arg_offset, &arg_tag))
			return;

	}
	load_tag_info_sym(arg_tag, arg, -arg_offset, star);
}

void register_container_of(int id)
{
	my_id = id;

	add_hook(&match_function_def, FUNC_DEF_HOOK);

	add_get_state_hook(&get_state_hook);

	add_hook(&match_save_states, INLINE_FN_START);
	add_hook(&match_restore_states, INLINE_FN_END);

	select_return_implies_hook(CONTAINER, &set_param_used);
	all_return_states_hook(&process_states);

	add_split_return_callback(&print_returns_container_of);
	select_return_states_hook(CONTAINER, &returns_container_of);

	add_hook(&match_call, FUNCTION_CALL_HOOK);
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return alloc_estate_whole(estate_type(sm->state));
}

void register_container_of2(int id)
{
	param_id = id;

	select_caller_info_hook(db_passed_container, CONTAINER);
	add_hook(&handle_passed_container, AFTER_DEF_HOOK);
	add_unmatched_state_hook(param_id, &unmatched_state);
	add_merge_hook(param_id, &merge_estates);
}

