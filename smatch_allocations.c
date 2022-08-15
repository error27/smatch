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
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

DECLARE_PTR_LIST(alloc_hook_list, alloc_hook);
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

	{"devm_kmalloc", "$1"},
	{"devm_kzalloc", "$1", .zeroed=true},
	{"devm_kmalloc_array", "$1 * $2"},
	{"devm_kcalloc", "$1 * $2", .zeroed=true},

	{"dma_alloc_attrs", "$1"},
	{"dma_alloc_coherent", "$1"},
	{"dma_alloc_consistent", "$1"},
	{"dma_alloc_contiguous", "$1"},

	{"krealloc", "$1"},

	{"kmalloc", "$0"},
	{"kmalloc_node", "$0"},
	{"kzalloc", "$0", .zeroed=true},
	{"kzalloc_node", "$0", .zeroed=true},

	{"kmalloc_array", "$0 * $1"},
	{"kcalloc", "$0 * $1", .zeroed=true},

	{"vmalloc", "$0"},
	{"__vmalloc", "$0"},
	{"vzalloc", "$0", .zeroed=true},

	{"kvmalloc", "$0"},
	{"kvmalloc_array", "$0 * $1"},
	{"kvzalloc", "$0", .zeroed=true},
	{"kvcalloc", "$0 * $1", .zeroed=true},

	{"kmemdup", "$1"},
	{"memdup_user", "$1"},

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

static void match_alloc(struct expression *expr, const char *name, struct symbol *sym, void *_info)
{
	struct alloc_fn_info *info = _info;
	struct allocation_info data = { };
	alloc_hook *fn;

	data.size_str = info->size;
	data.zeroed = info->zeroed;

	FOR_EACH_PTR(hook_funcs, fn) {
		fn(expr, name, sym, &data);
	} END_FOR_EACH_PTR(fn);
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
		add_function_param_key_hook(info->name, &match_alloc, -1, "$", info);
		info++;
	}
}
