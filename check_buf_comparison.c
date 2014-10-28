/*
 * Copyright (C) 2012 Oracle.
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
 * The point here is to store that a buffer has x bytes even if we don't know
 * the value of x.
 *
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int size_id;
static int link_id;

static int expr_equiv(struct expression *one, struct expression *two)
{
	struct symbol *one_sym, *two_sym;
	char *one_name = NULL;
	char *two_name = NULL;
	int ret = 0;

	if (!one || !two)
		return 0;
	if (one->type != two->type)
		return 0;
	one_name = expr_to_str_sym(one, &one_sym);
	if (!one_name || !one_sym)
		goto free;
	two_name = expr_to_str_sym(two, &two_sym);
	if (!two_name || !two_sym)
		goto free;
	if (one_sym != two_sym)
		goto free;
	if (strcmp(one_name, two_name) == 0)
		ret = 1;
free:
	free_string(one_name);
	free_string(two_name);
	return ret;
}

static void match_modify(struct sm_state *sm, struct expression *mod_expr)
{
	struct expression *expr;

	expr = sm->state->data;
	if (!expr)
		return;
	set_state_expr(size_id, expr, &undefined);
}

static struct smatch_state *alloc_expr_state(struct expression *expr)
{
	struct smatch_state *state;
	char *name;

	state = __alloc_smatch_state(0);
	expr = strip_expr(expr);
	name = expr_to_str(expr);
	state->name = alloc_sname(name);
	free_string(name);
	state->data = expr;
	return state;
}

static int bytes_per_element(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (!type)
		return 0;

	if (type->type != SYM_PTR && type->type != SYM_ARRAY)
		return 0;

	type = get_base_type(type);
	return type_bytes(type);
}

static void match_alloc(const char *fn, struct expression *expr, void *_size_arg)
{
	int size_arg = PTR_INT(_size_arg);
	struct expression *pointer, *call, *arg;
	struct sm_state *tmp;

	pointer = strip_expr(expr->left);
	call = strip_expr(expr->right);
	arg = get_argument_from_call_expr(call->args, size_arg);
	arg = strip_expr(arg);

	if (arg->type == EXPR_BINOP && arg->op == '*') {
		struct expression *left, *right;
		sval_t sval;

		left = strip_expr(arg->left);
		right = strip_expr(arg->right);

		if (get_implied_value(left, &sval) &&
		    sval.value == bytes_per_element(pointer))
			arg = right;

		if (get_implied_value(right, &sval) &&
		    sval.value == bytes_per_element(pointer))
			arg = left;
	}

	tmp = set_state_expr(size_id, pointer, alloc_expr_state(arg));
	if (!tmp)
		return;
	set_state_expr(link_id, arg, alloc_expr_state(pointer));
}

static void match_calloc(const char *fn, struct expression *expr, void *unused)
{
	struct expression *pointer, *call, *arg;
	struct sm_state *tmp;
	sval_t sval;

	pointer = strip_expr(expr->left);
	call = strip_expr(expr->right);
	arg = get_argument_from_call_expr(call->args, 0);
	if (get_implied_value(arg, &sval) &&
	    sval.value == bytes_per_element(pointer))
		arg = get_argument_from_call_expr(call->args, 1);

	tmp = set_state_expr(size_id, pointer, alloc_expr_state(arg));
	if (!tmp)
		return;
	set_state_expr(link_id, arg, alloc_expr_state(pointer));
}

static struct expression *get_saved_size(struct expression *buf)
{
	struct smatch_state *state;

	state = get_state_expr(size_id, buf);
	if (state)
		return state->data;
	return NULL;
}

static void array_check(struct expression *expr)
{
	struct expression *array;
	struct expression *size;
	struct expression *offset;
	char *array_str, *offset_str;

	expr = strip_expr(expr);
	if (!is_array(expr))
		return;

	array = strip_parens(expr->unop->left);
	size = get_saved_size(array);
	if (!size)
		return;
	offset = get_array_offset(expr);
	if (!possible_comparison(size, SPECIAL_EQUAL, offset))
		return;

	array_str = expr_to_str(array);
	offset_str = expr_to_str(offset);
	sm_msg("warn: potentially one past the end of array '%s[%s]'", array_str, offset_str);
	free_string(array_str);
	free_string(offset_str);
}

static void add_allocation_function(const char *func, void *call_back, int param)
{
	add_function_assign_hook(func, call_back, INT_PTR(param));
}

static char *buf_size_param_comparison(struct expression *array, struct expression_list *args)
{
	struct expression *arg;
	struct expression *size;
	static char buf[32];
	int i;

	size = get_saved_size(array);
	if (!size)
		return NULL;

	i = -1;
	FOR_EACH_PTR(args, arg) {
		i++;
		if (arg == array)
			continue;
		if (!expr_equiv(arg, size))
			continue;
		snprintf(buf, sizeof(buf), "==$%d", i);
		return buf;
	} END_FOR_EACH_PTR(arg);

	return NULL;
}

static void match_call(struct expression *call)
{
	struct expression *arg;
	char *compare;
	int param;

	param = -1;
	FOR_EACH_PTR(call->args, arg) {
		param++;
		if (!is_pointer(arg))
			continue;
		compare = buf_size_param_comparison(arg, call->args);
		if (!compare)
			continue;
		sql_insert_caller_info(call, ARRAY_LEN, param, "$$", compare);
	} END_FOR_EACH_PTR(arg);
}

static int get_param(int param, char **name, struct symbol **sym)
{
	struct symbol *arg;
	int i;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		/*
		 * this is a temporary hack to work around a bug (I think in sparse?)
		 * 2.6.37-rc1:fs/reiserfs/journal.o
		 * If there is a function definition without parameter name found
		 * after a function implementation then it causes a crash.
		 * int foo() {}
		 * int bar(char *);
		 */
		if (arg->ident->name < (char *)100)
			continue;
		if (i == param) {
			*name = arg->ident->name;
			*sym = arg;
			return TRUE;
		}
		i++;
	} END_FOR_EACH_PTR(arg);

	return FALSE;
}

static void set_param_compare(const char *array_name, struct symbol *array_sym, char *key, char *value)
{
	struct expression *array_expr;
	struct expression *size_expr;
	struct symbol *size_sym;
	char *size_name;
	long param;
	struct sm_state *tmp;

	if (strncmp(value, "==$", 3) != 0)
		return;
	param = strtol(value + 3, NULL, 10);
	if (!get_param(param, &size_name, &size_sym))
		return;
	array_expr = symbol_expression(array_sym);
	size_expr = symbol_expression(size_sym);

	tmp = set_state_expr(size_id, array_expr, alloc_expr_state(size_expr));
	if (!tmp)
		return;
	set_state_expr(link_id, size_expr, alloc_expr_state(array_expr));


}

static void munge_start_states(struct statement *stmt)
{
	struct state_list *slist = NULL;
	struct sm_state *sm;
	struct sm_state *poss;

	FOR_EACH_MY_SM(size_id, __get_cur_stree(), sm) {
		if (sm->state != &merged)
			continue;
		/*
		 * screw it.  let's just assume that if one caller passes the
		 * size then they all do.
		 */
		FOR_EACH_PTR(sm->possible, poss) {
			if (poss->state != &merged &&
			    poss->state != &undefined) {
				add_ptr_list(&slist, poss);
				break;
			}
		} END_FOR_EACH_PTR(poss);
	} END_FOR_EACH_SM(sm);

	FOR_EACH_PTR(slist, sm) {
		set_state(size_id, sm->name, sm->sym, sm->state);
	} END_FOR_EACH_PTR(sm);

	free_slist(&slist);
}

void check_buf_comparison(int id)
{
	size_id = id;

	add_allocation_function("malloc", &match_alloc, 0);
	add_allocation_function("memdup", &match_alloc, 1);
	add_allocation_function("realloc", &match_alloc, 1);
	if (option_project == PROJ_KERNEL) {
		add_allocation_function("kmalloc", &match_alloc, 0);
		add_allocation_function("kzalloc", &match_alloc, 0);
		add_allocation_function("vmalloc", &match_alloc, 0);
		add_allocation_function("__vmalloc", &match_alloc, 0);
		add_allocation_function("sock_kmalloc", &match_alloc, 1);
		add_allocation_function("kmemdup", &match_alloc, 1);
		add_allocation_function("kmemdup_user", &match_alloc, 1);
		add_allocation_function("dma_alloc_attrs", &match_alloc, 1);
		add_allocation_function("pci_alloc_consistent", &match_alloc, 1);
		add_allocation_function("pci_alloc_coherent", &match_alloc, 1);
		add_allocation_function("devm_kmalloc", &match_alloc, 1);
		add_allocation_function("devm_kzalloc", &match_alloc, 1);
		add_allocation_function("kcalloc", &match_calloc, 0);
		add_allocation_function("krealloc", &match_alloc, 1);
	}

	add_hook(&array_check, OP_HOOK);

	add_hook(&match_call, FUNCTION_CALL_HOOK);
	select_caller_info_hook(set_param_compare, ARRAY_LEN);
	add_hook(&munge_start_states, AFTER_DEF_HOOK);
}

void check_buf_comparison_links(int id)
{
	link_id = id;
	add_modification_hook(link_id, &match_modify);
}
