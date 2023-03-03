/*
 * Copyright (C) 2020 Oracle.
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

/* The below code is an adjustment of the smatch_points_to_user_data.c to
 * track (untrusted and potentially malicious) data pointers received by a
 * guest kernel via various low-level port IO, MMIO, MSR, CPUID and PCI
 * config space access functions (see func_table for a full list) from an
 * untrusted host/VMM under confidential computing threat model
 * (where a guest VM does not trust the host/VMM).
 * We call this data as 'host' data here. Being able to track host data
 * allows creating new smatch patterns that can perform various checks
 * on such data, i.e. verify that that there are no spectre v1 gadgets
 * on this attack surface or even simply report locations where host data
 * is being processed in the kernel source code (useful for a targeted code
 * audit).
 * Similar as smatch_points_to_user_data.c it works with
 * smatch_kernel_host_data.c code.
 *
 *  Copyright (C) 2022 Elena Reshetova Intel Corporation.
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;
STATE(host_data);
STATE(host_data_set);

static const char *returns_pointer_to_host_data[] = {
	"virtqueue_get_buf"
};

struct host_fn_info {
        const char *name;
        int type;
        int param;
        const char *key;
        const sval_t *implies_start, *implies_end;
        func_hook *call_back;
};

struct host_fn_info func_pointer_table[] = {
	    /* Functions that return host data as argument 1 */
        { "memcpy_fromio", HOST_DATA, 0, "*$" },
        { "acpi_os_read_iomem", HOST_DATA, 1, "*$" },
        { "mmio_insb", HOST_DATA, 1, "*$" },
        { "mmio_insw", HOST_DATA, 1, "*$" },
        { "mmio_insl", HOST_DATA, 1, "*$" },
        { "acpi_read_bit_register", HOST_DATA, 1, "*$" },
          /* Functions that return host data as argument 3 */
        { "__virtio_cread_many", HOST_DATA, 2, "*$" },
        { "pci_user_read_config_word", HOST_DATA, 2, "*$" },
        { "pci_user_read_config_dword", HOST_DATA, 2, "*$" },
        { "pci_user_read_config_byte", HOST_DATA, 2, "*$" },
          /* Functions that return host data as argument 4 */
        { "pci_bus_read_config_byte", HOST_DATA, 3, "*$" },
        { "pci_bus_read_config_word", HOST_DATA, 3, "*$" },
        { "pci_bus_read_config_dword", HOST_DATA, 3, "*$" },
       	/* Functions that return host data as argument 6 */
        { "raw_pci_read", HOST_DATA, 5, "*$" },
       	/* Functions that return host data as arguments 2-5 */
        { "cpuid", HOST_DATA, 1, "*$" },
        { "cpuid", HOST_DATA, 2, "*$" },
        { "cpuid", HOST_DATA, 3, "*$" },
        { "cpuid", HOST_DATA, 4, "*$" },
        /* Functions that return host data as arguments 3-6 */
        { "cpuid_count", HOST_DATA, 2, "*$" },
        { "cpuid_count", HOST_DATA, 3, "*$" },
        { "cpuid_count", HOST_DATA, 4, "*$" },
        { "cpuid_count", HOST_DATA, 5, "*$" },
};

bool is_host_data_fn(struct symbol *fn)
{
	int i;

	if (!fn || !fn->ident)
		return false;

	for (i = 0; i < ARRAY_SIZE(returns_pointer_to_host_data); i++) {
		if (strcmp(fn->ident->name, returns_pointer_to_host_data[i]) == 0) {
//			func_gets_host_data = true;
			return true;
		}
	}
	return false;
}

bool is_fn_points_to_host_data(const char *fn)
{
	int i;

	if (!fn)
		return false;

	for (i = 0; i < ARRAY_SIZE(returns_pointer_to_host_data); i++) {
		if (strcmp(fn, returns_pointer_to_host_data[i]) == 0) {
			return true;
		}
	}
	return false;
}

static bool is_points_to_host_data_fn(struct expression *expr)
{
	expr = strip_expr(expr);
	if (!expr || expr->type != EXPR_CALL || expr->fn->type != EXPR_SYMBOL ||
	    !expr->fn->symbol)
		return false;
	return is_host_data_fn(expr->fn->symbol);
}

static bool is_array_of_host_data(struct expression *expr)
{
	struct expression *deref;
	struct symbol *type;

	if (expr->type == EXPR_PREOP && expr->op == '&') {
		expr = strip_expr(expr->unop);
		if (expr->type == EXPR_PREOP && expr->op == '*')
			expr = strip_expr(expr->unop);
	}

	/* This is for array elements &foo->data[4] */
	if (expr->type == EXPR_BINOP && expr->op == '+') {
		if (points_to_host_data(expr->left))
			return true;
		if (points_to_host_data(expr->right))
			return true;
	}

	/* This is for if you have: foo = skb->data; frob(foo->array); */
	type = get_type(expr);
	if (!type || type->type != SYM_ARRAY)
		return false;

	if (expr->type != EXPR_DEREF)
		return false;
	deref = strip_expr(expr->deref);
	if (deref->type != EXPR_PREOP || deref->op != '*')
		return false;
	deref = strip_expr(deref->unop);
	return points_to_host_data(deref);
}

bool points_to_host_data(struct expression *expr)
{
	struct sm_state *sm;

	if (!expr)
		return false;

	expr = strip_expr(expr);
	if (!expr)
		return false;

	if (is_fake_call(expr))
		return false;

	if (expr->type == EXPR_ASSIGNMENT)
		return points_to_host_data(expr->left);

	if (is_array_of_host_data(expr))
		return true;

	if (expr->type == EXPR_BINOP && expr->op == '+')
		expr = strip_expr(expr->left);

	if (is_points_to_host_data_fn(expr))
		return true;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm)
		return false;
	if (slist_has_state(sm->possible, &host_data) ||
	    slist_has_state(sm->possible, &host_data_set))
		return true;
	return false;
}

void set_points_to_host_data(struct expression *expr, bool is_new)
{
	struct expression *tmp;

	tmp = get_assigned_expr(expr);
	if (tmp)
		set_state_expr(my_id, tmp, is_new ? &host_data_set : &host_data);
	set_state_expr(my_id, expr, is_new ? &host_data_set : &host_data);
}

static void match_assign_host(struct expression *expr)
{

	if (is_fake_call(expr->right))
		return;

	if (!is_ptr_type(get_type(expr->left))){
		return;
	}

	if (points_to_host_data(expr->right)) {
		set_points_to_host_data(expr->left, false);
		return;
	}

	if (get_state_expr(my_id, expr->left)){
		set_state_expr(my_id, expr->left, &undefined);
	}
}

static void match_memcpy_host(const char *fn, struct expression *expr, void *_unused)
{
	struct expression *dest, *src;

	dest = get_argument_from_call_expr(expr->args, 0);
	src = get_argument_from_call_expr(expr->args, 1);

	if (points_to_host_data(src)) {
		set_points_to_host_data(dest, false);
		return;
	}

	if (get_state_expr(my_id, dest))
		set_state_expr(my_id, dest, &undefined);
}

static void match_host_function(const char *fn, struct expression *expr, void *_unused)
{
	struct expression *dest;

	for (int i = 0; i < ARRAY_SIZE(func_pointer_table); i++)
		if (sym_name_is(func_pointer_table[i].name, expr->fn)){
			dest = get_argument_from_call_expr(expr->args, func_pointer_table[i].param);
			dest = strip_expr(dest);
			if (!dest)
				return;
			set_state_expr(my_id, dest, &host_data);
		}
}

static void return_info_callback_host(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	int type;

	if (strncmp(printed_name, "&$", 2) == 0)
		return;

	if (param >= 0) {
		if (!slist_has_state(sm->possible, &host_data_set))
			return;
		type = HOST_PTR_SET;
	} else {
		if (slist_has_state(sm->possible, &host_data_set))
			type = HOST_PTR_SET;
		else if (slist_has_state(sm->possible, &host_data))
			type = HOST_PTR;
		else
			return;
	}
	if (parent_is_gone_var_sym(sm->name, sm->sym))
		return;

	sql_insert_return_states(return_id, return_ranges, type,
				 param, printed_name, "");
}

static void returns_host_ptr_helper(struct expression *expr, int param, char *key, char *value, bool set)
{
	struct expression *arg;
	struct expression *call;
	char *name;
	struct symbol *sym;

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (call->type != EXPR_CALL)
		return;

	if (!set && !we_pass_host_data(call))
		return;

	if (param == -1) {
		if (expr->type != EXPR_ASSIGNMENT) {
			/* Nothing to do.  Fake assignments should handle it */
			return;
		}
		arg = expr->left;
		goto set_host;
	}

	arg = get_argument_from_call_expr(call->args, param);
	if (!arg)
		return;
set_host:
	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;
	if (set)
		set_state(my_id, name, sym, &host_data_set);
	else
		set_state(my_id, name, sym, &host_data);
free:
	free_string(name);
}

static void returns_host_ptr(struct expression *expr, int param, char *key, char *value)
{
	returns_host_ptr_helper(expr, param, key, value, false);
}

static void returns_host_ptr_set(struct expression *expr, int param, char *key, char *value)
{
	returns_host_ptr_helper(expr, param, key, value, true);
}

static void set_param_host_ptr(const char *name, struct symbol *sym, char *key, char *value)
{
	struct expression *expr;
	char *fullname;

	expr = symbol_expression(sym);
	fullname = get_variable_from_key(expr, key, NULL);
	if (!fullname)
		return;
	set_state(my_id, fullname, sym, &host_data);
}

static void caller_info_callback_host(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	if (!slist_has_state(sm->possible, &host_data) &&
	    !slist_has_state(sm->possible, &host_data_set))
		return;
	sql_insert_caller_info(call, HOST_PTR, param, printed_name, "");
}

void register_points_to_host_data(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(&match_assign_host, ASSIGNMENT_HOOK);

	for (int i = 0; i < ARRAY_SIZE(func_pointer_table); i++)
		add_function_hook(func_pointer_table[i].name, &match_host_function, NULL);

	add_function_hook("memcpy", &match_memcpy_host, NULL);
	add_function_hook("__memcpy", &match_memcpy_host, NULL);

	add_caller_info_callback(my_id, caller_info_callback_host);
	add_return_info_callback(my_id, return_info_callback_host);

	select_caller_info_hook(set_param_host_ptr, HOST_PTR);
	select_return_states_hook(HOST_PTR, &returns_host_ptr);
	select_return_states_hook(HOST_PTR_SET, &returns_host_ptr_set);
}
