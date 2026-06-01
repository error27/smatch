/*
 * Copyright (C) 2021 Oracle.
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

#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include "parse.h"
#include "smatch.h"

static int my_id;

DECLARE_PTR_LIST(alloc_hook_list, alloc_hook);
static struct alloc_hook_list *hook_funcs_early;
static struct alloc_hook_list *hook_funcs;

struct alloc_fn_info {
	const char *name;
	const char *size;
	bool zeroed;
};

static struct alloc_fn_info alloc_fns[] = {
	{"malloc", "$0"},
	{"calloc", "$0 * $1", .zeroed=true},
	{"memdup", "$1"},
	{"realloc", "$1"},
	{ },
};

static struct alloc_fn_info kernel_alloc_funcs[] = {
	{"__alloc_skb", "$0"},

	{"comedi_alloc_spriv", "$1", .zeroed=true},

	{"devm_kmalloc", "$1"},
	{"devm_kzalloc", "$1", .zeroed=true},
	{"devm_kmalloc_array", "$1 * $2"},
	{"devm_kcalloc", "$1 * $2", .zeroed=true},

	{"dma_alloc_attrs", "$1", .zeroed=true},
	{"dma_alloc_coherent", "$1", .zeroed=true},
	{"dma_alloc_contiguous", "$1"},

	{"krealloc", "$1"},

	{"kmalloc", "$0"},
	{"kmalloc_node", "$0"},
	{"kmalloc_noprof", "$0"},
	{"kzalloc", "$0", .zeroed=true},
	{"kzalloc_node", "$0", .zeroed=true},
	{"kzalloc_noprof", "$0", .zeroed=true},

	{"kmalloc_array", "$0 * $1"},
	{"kmalloc_array_noprof", "$0 * $1"},
	{"kcalloc", "$0 * $1", .zeroed=true},

	{"vmalloc", "$0"},
	{"__vmalloc", "$0"},
	{"vzalloc", "$0", .zeroed=true},
	{"vzalloc_noprof", "$0", .zeroed=true},

	{"kunit_kmalloc_array", "$1 * $2"},
	{"kunit_kmalloc", "$1"},
	{"kunit_kzalloc", "$1", .zeroed=true},

	{"kvmalloc", "$0"},
	{"kvmalloc_array", "$0 * $1"},
	{"kvmalloc_node", "$0"},
	{"kvzalloc", "$0", .zeroed=true},
	{"kvcalloc", "$0 * $1", .zeroed=true},
	{"kvzalloc_node", "$0", .zeroed=true},
	{"kvrealloc", "$2"},

	{"kmemdup", "$1"},
	{"devm_kmemdup", "$2"},
	{"memdup_user", "$1"},

	{"sk_alloc", ""},
	{"sk_prot_alloc", ""},
	{"sock_kmalloc", "$1"},

#if 0
	{"get_zeroed_page", {"PAGE_SIZE", zeroed=true}},
	{"alloc_page", {"PAGE_SIZE"}},
	{"alloc_pages", {"(1 < $0) * PAGE_SIZE"}},
	{"alloc_pages_current", {"(1 < $0) * PAGE_SIZE"}},
	{"__get_free_pages", {"(1 < $0) * PAGE_SIZE"}},
#endif
	{ },
};

struct alloc_fn_info *alloc_table;

void add_allocation_hook(alloc_hook *hook)
{
	add_ptr_list(&hook_funcs, hook);
}

void add_allocation_hook_early(alloc_hook *hook)
{
	add_ptr_list(&hook_funcs_early, hook);
}

static struct alloc_fn_info *get_info_from_table(struct expression *expr)
{
	struct alloc_fn_info *info;
	const char *name;

	expr = get_rightmost_call(expr);
	if (!expr)
		return NULL;

	name = get_fn_name(expr);
	if (!name)
		return NULL;

	for (info = &alloc_table[0]; info->name; info++) {
		if (strcmp(info->name, name) == 0)
			return info;
	}
	return NULL;
}

static bool in_alloc_table(struct expression *expr)
{
	if (get_info_from_table(expr))
		return true;
	return false;
}

bool is_allocation_primitive(struct expression *expr)
{
	struct expression *call;

	if (in_alloc_table(expr))
		return true;

	call = get_rightmost_call(expr);
	if (!call || !call->fn || call->fn->type != EXPR_SYMBOL)
		return false;
	if (!call->fn->symbol || !call->fn->symbol->alloc_size)
		return false;

	return true;
}

static void set_nr_size(struct allocation_info *data, struct expression *arg1, struct expression *arg2)
{
	struct expression *tmp;
	bool swap = false;
	sval_t dummy;

	arg1 = strip_parens(arg1);
	if (!arg2) {
		tmp = get_assigned_expr(arg1);
		if (tmp)
			arg1 = tmp;
		if (!arg1 || arg1->type != EXPR_BINOP || arg1->op != '*')
			return;
		tmp = arg1;
		arg1 = tmp->left;
		arg2 = tmp->right;
	}

	if (!arg1 || !arg2)
		return;

	if (arg2->type == EXPR_SIZEOF)
		swap = false;
	else if (arg1->type == EXPR_SIZEOF)
		swap = true;
	else if (get_value(arg2, &dummy))
		swap = false;
	else if (get_value(arg1, &dummy))
		swap = true;

	if (swap) {
		data->nr_elems = arg2;
		data->elem_size = arg1;
	} else {
		data->nr_elems = arg1;
		data->elem_size = arg2;
	}
}

static bool handle_size_mul(struct allocation_info *data, struct expression *expr)
{
	struct expression *tmp, *arg1, *arg2;

	tmp = get_assigned_expr(expr);
	if (tmp)
		expr = tmp;
	if (!expr || expr->type != EXPR_CALL || !sym_name_is(expr->fn, "size_mul"))
		return false;

	arg1 = get_argument_from_call_expr(expr->args, 0);
	arg2 = get_argument_from_call_expr(expr->args, 1);
	if (!arg1 || !arg2)
		return false;

	set_nr_size(data, arg1, arg2);
	data->total_size = binop_expression(arg1, '*', arg2);
	return true;
}

static void load_size_data(struct allocation_info *data, struct expression *expr, const char *size_str)
{
	struct expression *call, *arg1, *arg2;
	struct range_list *rl;
	const char *p;
	int op;
	int param;

	if (!size_str)
		return;

	p = size_str;
	if (*p != '$')
		return;
	p++;
	if (!isdigit(*p))
		return;
	param = atoi(p);

	call = get_rightmost_call(expr);
	if (!call)
		return;
	arg1 = get_argument_from_call_expr(call->args, param);
	if (!arg1)
		return;

	p++;
	if (*p == '\0') {
		if (!handle_size_mul(data, arg1)) {
			data->total_size = arg1;
			set_nr_size(data, arg1, NULL);
		}
		get_absolute_rl(data->total_size, &rl);
		data->size_rl = cast_rl(&ulong_ctype, rl);
		return;
	}
	while (*p == ' ')
		p++;
	op = *p;
	if (op != '*' && op != '+')
		return;
	p++;
	while (p[0] == ' ')
		p++;
	if (p[0] != '$' || !isdigit(p[1]))
		return;
	p++;
	param = atoi(p);
	arg2 = get_argument_from_call_expr(call->args, param);
	if (!arg2)
		return;

	data->total_size = binop_expression(arg1, op, arg2);
	if (op == '*')
		set_nr_size(data, arg1, arg2);
	get_absolute_rl(data->total_size, &rl);
	data->size_rl = cast_rl(&ulong_ctype, rl);
}

#define SIZE_STR_MAX 64
static bool load_alloc_fn_info_from_attribute(struct expression *expr, struct alloc_fn_info *info)
{
	struct expression *call;
	struct symbol *fn_sym;

	call = get_rightmost_call(expr);
	if (!call || !call->fn || call->fn->type != EXPR_SYMBOL)
		return false;

	fn_sym = call->fn->symbol;
	if (!fn_sym || !fn_sym->alloc_size)
		return false;
	if (!fn_sym->ident)
		return false;

	info->name = fn_sym->ident->name;
	if (fn_sym->alloc_size->param2 == -1)
		snprintf((char *)info->size, SIZE_STR_MAX, "$%d", fn_sym->alloc_size->param1 - 1);
	else
		snprintf((char *)info->size, SIZE_STR_MAX, "$%d * $%d",
			 fn_sym->alloc_size->param1 - 1,
			 fn_sym->alloc_size->param2 - 1);
	if (strstr(info->name, "zalloc") || strstr(info->name, "calloc"))
		info->zeroed = true;

	return true;
}

bool load_allocation_info(struct expression *expr, struct allocation_info *info)
{
	struct alloc_fn_info *fn_info;
	struct alloc_fn_info info_attrib = {};
	char buf[SIZE_STR_MAX];

	expr = get_rightmost_call(expr);
	if (!expr)
		return false;

	fn_info = get_info_from_table(expr);
	if (fn_info)
		goto found;

	info_attrib.size = buf;
	if (load_alloc_fn_info_from_attribute(expr, &info_attrib)) {
		fn_info = &info_attrib;
		goto found;
	}

	return false;
found:
	info->fn_name = fn_info->name;
	info->size_str = fn_info->size;
	info->zeroed = fn_info->zeroed;
	load_size_data(info, expr, fn_info->size);
	return true;
}

static void match_alloc_helper(struct alloc_hook_list *hooks, struct expression *expr, const char *name, struct symbol *sym, void *_info)
{
	struct alloc_fn_info *info = _info;
	struct allocation_info data = { };
	alloc_hook *fn;

	data.fn_name = info->name;
	data.size_str = info->size;
	data.zeroed = info->zeroed;
	load_size_data(&data, expr, info->size);

	FOR_EACH_PTR(hooks, fn) {
		fn(expr, name, sym, &data);
	} END_FOR_EACH_PTR(fn);
}

static void match_alloc_early(struct expression *expr, const char *name, struct symbol *sym, void *_info)
{
	match_alloc_helper(hook_funcs_early, expr, name, sym, _info);
}

static void match_alloc(struct expression *expr, const char *name, struct symbol *sym, void *_info)
{
	match_alloc_helper(hook_funcs, expr, name, sym, _info);
}

static void match_assign_call_helper(struct expression *expr, bool early)
{
	struct alloc_fn_info info = {};
	struct expression *left;
	char buf[SIZE_STR_MAX];
	struct symbol *sym;
	char *name;

	info.size = buf;
	if (!load_alloc_fn_info_from_attribute(expr, &info))
		return;

	if (early) {
		struct expression *parent;

		parent = expr;
		while (parent && parent->type != EXPR_ASSIGNMENT)
			parent = expr_get_parent_expr(parent);
		if (!parent)
			return;
		left = parent->left;
	} else {
		left = expr->left;
	}

	name = expr_to_str_sym(left, &sym);
	if (!name || !sym)
		return;

	match_alloc_helper(early ? hook_funcs_early : hook_funcs, expr,
			   name, sym, &info);
}

static void match_assign_call_early(struct expression *expr)
{
	match_assign_call_helper(expr, true);
}

static void match_assign_call(struct expression *expr)
{
	match_assign_call_helper(expr, false);
}

void smatch_allocations(int id)
{
	struct alloc_fn_info *info;

	my_id = id;

	add_hook(&match_assign_call_early, CALL_ASSIGNMENT_HOOK);
	add_hook(&match_assign_call, CALL_ASSIGNMENT_HOOK);

	if (option_project == PROJ_KERNEL)
		alloc_table = kernel_alloc_funcs;
	else
		alloc_table = alloc_fns;

	info = alloc_table;
	while (info->name) {
		add_function_param_key_hook_early(info->name, &match_alloc_early, -1, "$", info);
		add_function_param_key_hook(info->name, &match_alloc, -1, "$", info);
		info++;
	}
}
