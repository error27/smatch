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
#include "smatch_extra.h"
#include "smatch_slist.h"

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

void add_allocation_hook(alloc_hook *hook)
{
	add_ptr_list(&hook_funcs, hook);
}

void add_allocation_hook_early(alloc_hook *hook)
{
	add_ptr_list(&hook_funcs_early, hook);
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

static void load_size_data(struct allocation_info *data, struct expression *expr, const char *size_str)
{
	struct expression *call, *arg1, *arg2;
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

	call = get_assigned_call(expr);
	if (!call)
		return;
	arg1 = get_argument_from_call_expr(call->args, param);
	if (!arg1)
		return;

	p++;
	if (*p == '\0') {
		data->total_size = arg1;
		set_nr_size(data, arg1, NULL);
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

void register_allocations(int id)
{
	struct alloc_fn_info *info;

	my_id = id;

	if (option_project == PROJ_KERNEL)
		info = kernel_alloc_funcs;
	else
		info = alloc_fns;

	while (info->name) {
		add_function_param_key_hook_early(info->name, &match_alloc_early, -1, "$", info);
		add_function_param_key_hook(info->name, &match_alloc, -1, "$", info);
		info++;
	}
}
