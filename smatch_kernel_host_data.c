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

/* The below code is an adjustment of the smatch_kernel_user_data.c to
 * track (untrusted and potentially malicious) data received by a guest
 * kernel via various low-level port IO, MMIO, MSR, CPUID and PCI config
 * space access functions (see func_table for a full list) from an
 * untrusted host/VMM under confidential computing threat model
 * (where a guest VM does not trust the host/VMM).
 * We call this data as 'host' data here. Being able to track host data
 * allows creating new smatch patterns that can perform various checks
 * on such data, i.e. verify that that there are no spectre v1 gadgets
 * on this attack surface or even simply report locations where host data
 * is being processed in the kernel source code (useful for a targeted code
 * audit).
 * Similar as smatch_kernel_user_data.c it works with
 * smatch_points_to_host_input.c code.
 *
 * Copyright (C) 2022 Elena Reshetova Intel Corporation.
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

struct host_fn_info {
	const char *name;
	int type;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
	func_hook *call_back;
};

static struct host_fn_info func_table[] = {
	  /* Functions that return host data as return value */
	{ "inb", HOST_DATA, -1, "$" },
	{ "inw", HOST_DATA, -1, "$" },
	{ "inl", HOST_DATA, -1, "$" },
	{ "__inb", HOST_DATA, -1, "$" },
	{ "__inw", HOST_DATA, -1, "$" },
	{ "__inl", HOST_DATA, -1, "$" },
	{ "ioread8", HOST_DATA, -1, "$" },
	{ "ioread16", HOST_DATA, -1, "$" },
	{ "ioread32", HOST_DATA, -1, "$" },
	{ "ioread16be", HOST_DATA, -1, "$" },
	{ "ioread32be", HOST_DATA, -1, "$" },
	{ "ioread64_lo_hi", HOST_DATA, -1, "$" },
	{ "ioread64_hi_lo", HOST_DATA, -1, "$" },
	{ "ioread64be_lo_hi", HOST_DATA, -1, "$" },
	{ "ioread64be_hi_lo", HOST_DATA, -1, "$" },
	{ "iomap_readq", HOST_DATA, -1, "$" },
	{ "iomap_readb", HOST_DATA, -1, "$" },
	{ "iomap_readw", HOST_DATA, -1, "$" },
	{ "iomap_readl", HOST_DATA, -1, "$" },
	{ "readb", HOST_DATA, -1, "$" },
	{ "readw", HOST_DATA, -1, "$" },
	{ "readl", HOST_DATA, -1, "$" },
	{ "readq", HOST_DATA, -1, "$" },
	{ "__raw_readb", HOST_DATA, -1, "$" },
	{ "__raw_readw", HOST_DATA, -1, "$" },
	{ "__raw_readl", HOST_DATA, -1, "$" },
	{ "__raw_readq", HOST_DATA, -1, "$" },
	{ "__readq", HOST_DATA, -1, "$" },
	{ "__readl", HOST_DATA, -1, "$" },
	{ "__readb", HOST_DATA, -1, "$" },
	{ "__readw", HOST_DATA, -1, "$" },
	{ "native_read_msr", HOST_DATA, -1, "$" },
	{ "native_read_msr_safe", HOST_DATA, -1, "$" },
	{ "__rdmsr", HOST_DATA, -1, "$" },
	{ "paravirt_read_msr", HOST_DATA, -1, "$" },
	{ "paravirt_read_msr_safe", HOST_DATA, -1, "$" },
	  /* the below 3 apic funcs are needed
	   * since they unwrap to apic->read(reg),
	   * apic->icr_read() and *(APIC_BASE + reg) */
	{ "apic_read", HOST_DATA, -1, "$" },
	{ "apic_icr_read", HOST_DATA, -1, "$" },
	{ "native_apic_mem_read", HOST_DATA, -1, "$" },
	  /* the below one is x86_apic_ops.io_apic_read */
	{ "io_apic_read", HOST_DATA, -1, "$" },
	  /* virtio_cread8/16/32/64 funcs are needed
	   * since they call vdev->config->get */
	{ "virtio_cread8", HOST_DATA, -1, "$" },
	{ "virtio_cread16", HOST_DATA, -1, "$" },
	{ "virtio_cread32", HOST_DATA, -1, "$" },
	{ "virtio_cread64", HOST_DATA, -1, "$" },
	{ "__virtio16_to_cpu", HOST_DATA, -1, "$" },
	{ "__virtio32_to_cpu", HOST_DATA, -1, "$" },
	{ "__virtio64_to_cpu", HOST_DATA, -1, "$" },
	{ "serial_dl_read", HOST_DATA, -1, "$" },
	{ "serial_in", HOST_DATA, -1, "$" },
	{ "serial_port_in", HOST_DATA, -1, "$" },
	{ "cpuid_eax", HOST_DATA, -1, "$" },
	{ "cpuid_ebx", HOST_DATA, -1, "$" },
	{ "cpuid_ecx", HOST_DATA, -1, "$" },
	{ "cpuid_edx", HOST_DATA, -1, "$" },
	  /* Functions that return host data as argument 1 */
	{ "memcpy_fromio", HOST_DATA, 0, "*$" },
	  /* Functions that return host data as argument 2 */
	{ "acpi_os_read_iomem", HOST_DATA, 1, "*$" },
	{ "mmio_insb", HOST_DATA, 1, "*$" },
	{ "mmio_insw", HOST_DATA, 1, "*$" },
	{ "mmio_insl", HOST_DATA, 1, "*$" },
	{ "acpi_read_bit_register", HOST_DATA, 1, "*$" },
	  /* Functions that return host data as argument 3 */
	{ "rdmsrl_on_cpu", HOST_DATA, 2, "*$" },
	{ "rdmsrl_safe_on_cpu", HOST_DATA, 2, "*$" },
	{ "(struct virtio_config_ops)->get", HOST_DATA, 2, "*$" },
	{ "__virtio_cread_many", HOST_DATA, 2, "*$" },
	{ "pci_user_read_config_word", HOST_DATA, 2, "*$" },
	{ "pci_user_read_config_dword", HOST_DATA, 2, "*$" },
	{ "pci_user_read_config_byte", HOST_DATA, 2, "*$" },
	  /* Functions that return host data as argument 4 */
	{ "pci_bus_read_config_byte", HOST_DATA, 3, "*$" },
	{ "pci_bus_read_config_word", HOST_DATA, 3, "*$" },
	{ "pci_bus_read_config_dword", HOST_DATA, 3, "*$" },
	  /* Functions that return host data as arguments 3 and 4 */
	{ "rdmsr_on_cpu", HOST_DATA, 2, "*$" },
	{ "rdmsr_on_cpu", HOST_DATA, 3, "*$" },
	{ "rdmsr_safe_on_cpu", HOST_DATA, 2, "*$" },
	{ "rdmsr_safe_on_cpu", HOST_DATA, 3, "*$" },
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

int get_host_data_fn_param(const char *fn)
{
	int ret = 0;

	if (!fn)
		return ret;

	for (int i = 0; i < ARRAY_SIZE(func_table); i++) {
		if (strcmp(fn, func_table[i].name) == 0) {
			ret = func_table[i].param;
		}
	}
	if ((!ret) && (is_fn_points_to_host_data(fn))) {
		ret = -1;
	}

	return ret;
}

static int my_id;
static unsigned long func_gets_host_data;
static struct stree *start_states;

static struct smatch_state *empty_state(struct sm_state *sm)
{
	return alloc_estate_empty();
}

static struct smatch_state *new_state(struct symbol *type)
{
	struct smatch_state *state;

	if (!type || type_is_ptr(type))
		return NULL;

	state = alloc_estate_whole(type);
	estate_set_new(state);
	return state;
}

static void pre_merge_hook(struct sm_state *cur, struct sm_state *other)
{
	struct smatch_state *kernel = cur->state;
	struct smatch_state *extra;
	struct smatch_state *state;
	struct range_list *rl;

	extra = __get_state(SMATCH_EXTRA, cur->name, cur->sym);
	if (!extra)
		return;
	rl = rl_intersection(estate_rl(kernel), estate_rl(extra));
	state = alloc_estate_rl(clone_rl(rl));
	if (estate_capped(kernel) || is_capped_var_sym(cur->name, cur->sym))
		estate_set_capped(state);
	if (estates_equiv(state, cur->state))
		return;
	if (estate_new(cur->state))
		estate_set_new(state);
	set_state(my_id, cur->name, cur->sym, state);
}

static void extra_nomod_hook(const char *name, struct symbol *sym, struct expression *expr, struct smatch_state *state)
{
	struct smatch_state *host, *new;
	struct range_list *rl;

	host = __get_state(my_id, name, sym);
	if (!host)
		return;

	rl = rl_intersection(estate_rl(host), estate_rl(state));
	if (rl_equiv(rl, estate_rl(host)))
		return;
	new = alloc_estate_rl(rl);
	if (estate_capped(host))
		estate_set_capped(new);
	set_state(my_id, name, sym, new);
}

static void store_type_info(struct expression *expr, struct smatch_state *state)
{
	struct symbol *type;
	char *type_str, *member;

	if (__in_fake_assign)
		return;

	if (!estate_rl(state))
		return;

	expr = strip_expr(expr);
	if (!expr || expr->type != EXPR_DEREF || !expr->member)
		return;

	type = get_type(expr->deref);
	if (!type || !type->ident)
		return;

	type_str = type_to_str(type);
	if (!type_str)
		return;
	member = get_member_name(expr);
	if (!member)
		return;

	sql_insert_function_type_info(HOST_DATA, type_str, member, state->name);
}

static void set_host_data(struct expression *expr, struct smatch_state *state)
{
	store_type_info(expr, state);
	set_state_expr(my_id, expr, state);
}

static bool host_rl_known(struct expression *expr)
{
	struct range_list *rl;
	sval_t close_to_max;

	if (!get_host_rl(expr, &rl))
		return true;

	close_to_max = sval_type_max(rl_type(rl));
	close_to_max.value -= 100;

	if (sval_cmp(rl_max(rl), close_to_max) >= 0)
		return false;
	return true;
}

static bool is_array_index_mask_nospec(struct expression *expr)
{
	struct expression *orig;

	orig = get_assigned_expr(expr);
	if (!orig || orig->type != EXPR_CALL)
		return false;
	return sym_name_is("array_index_mask_nospec", orig->fn);
}

static bool binop_capped(struct expression *expr)
{
	struct range_list *left_rl;
	int comparison;
	sval_t sval;

	if (expr->op == '-' && get_host_rl(expr->left, &left_rl)) {
		if (host_rl_capped(expr->left))
			return true;
		comparison = get_comparison(expr->left, expr->right);
		if (comparison && show_special(comparison)[0] == '>')
			return true;
		return false;
	}

	if (expr->op == '&' || expr->op == '%') {
		bool left_host, left_capped, right_host, right_capped;

		if (!get_value(expr->right, &sval) && is_capped(expr->right))
			return true;
		if (is_array_index_mask_nospec(expr->right))
			return true;
		if (is_capped(expr->left))
			return true;
		left_host = is_host_rl(expr->left);
		right_host = is_host_rl(expr->right);
		if (!left_host && !right_host)
			return true;

		left_capped = host_rl_capped(expr->left);
		right_capped = host_rl_capped(expr->right);

		if (left_host && left_capped) {
			if (!right_host)
				return true;
			if (right_host && right_capped)
				return true;
			return false;
		}
		if (right_host && right_capped) {
			if (!left_host)
				return true;
			return false;
		}
		return false;
	}

	/*
	 * Generally "capped" means that we capped it to an unknown value.
	 * This is useful because if Smatch doesn't know what the value is then
	 * we have to trust that it is correct.  But if we known cap value is
	 * 100 then we can check if 100 is correct and complain if it's wrong.
	 *
	 * So then the problem is with BINOP when we take a capped variable
	 * plus a host variable which is clamped to a known range (uncapped)
	 * the result should be capped.
	 */
	if ((host_rl_capped(expr->left) || host_rl_known(expr->left)) &&
	    (host_rl_capped(expr->right) || host_rl_known(expr->right)))
		return true;
	return false;
}

bool host_rl_capped(struct expression *expr)
{
	struct smatch_state *state;
	struct range_list *rl;
	sval_t sval;

	expr = strip_expr(expr);
	if (!expr)
		return false;
	if (get_value(expr, &sval))
		return true;
	if (expr->type == EXPR_BINOP)
		return binop_capped(expr);
	if ((expr->type == EXPR_PREOP || expr->type == EXPR_POSTOP) &&
	    (expr->op == SPECIAL_INCREMENT || expr->op == SPECIAL_DECREMENT))
		return host_rl_capped(expr->unop);
	state = get_state_expr(my_id, expr);
	if (state)
		return estate_capped(state);

	if (!get_host_rl(expr, &rl)) {
		/*
		 * The non host data parts of a binop are capped and
		 * also empty host rl states are capped.
		 */
		return true;
	}

	if (rl_to_sval(rl, &sval))
		return true;

	return false;  /* uncapped host data */
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
		set_host_data(edge_member, new_state(type));
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
		if (type->type == SYM_ARRAY) {
			set_points_to_host_data(member, true);
		} else {
			set_host_data(member, new_state(get_type(member)));
		}
	} END_FOR_EACH_PTR(tmp);
}

static void tag_base_type(struct expression *expr)
{
	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = strip_expr(expr->unop);
	else
		expr = deref_expression(expr);
	set_host_data(expr, new_state(get_type(expr)));
}

static void tag_as_host_data(struct expression *expr)
{
	struct symbol *type;

	expr = strip_expr(expr);
	type = get_type(expr);

	if (!type)
		return;

	if (type->type == SYM_PTR){
		type = get_real_base_type(type);
		if (!type)
			return;
		if (type == &void_ctype) {
			set_host_data(deref_expression(expr), new_state(&ulong_ctype));
			return;
		}
		if (type->type == SYM_BASETYPE) {
			if (expr->type != EXPR_PREOP && expr->op != '&')
				set_points_to_host_data(expr, true);
			tag_base_type(expr);
			return;
		}
		if (type->type == SYM_STRUCT || type->type == SYM_UNION) {
			if (expr->type != EXPR_PREOP || expr->op != '&')
				expr = deref_expression(expr);
			else
				set_host_data(deref_expression(expr), new_state(&ulong_ctype));
			tag_struct_members(type, expr);
		}
		return;
	}

	if (expr->type == EXPR_DEREF) {
  		type = get_type(expr->deref);
  		expr = strip_expr(expr->deref);

  		if (!type){
  			sm_msg("%s: no type\n", __func__);
  			return;
  		}
  	}

	if (type->type == SYM_BASETYPE) {
		if (expr->type == EXPR_PREOP && expr->op == '*')
			set_points_to_host_data(expr->unop, true);
		set_host_data(expr, new_state(get_type(expr)));
		return;
	}
	if (type->type == SYM_STRUCT || type->type == SYM_UNION) {
		set_host_data(expr, new_state(&ulong_ctype));
		tag_struct_members(type, expr);
	}
}

static int get_rl_from_function(struct expression *expr, struct range_list **rl)
{
	if (!expr)
		return 0;

	if (expr->type != EXPR_CALL || expr->fn->type != EXPR_SYMBOL ||
	    !expr->fn->symbol_name)
		return 0;

	for (int i = 0; i < ARRAY_SIZE(func_table); i++) {
		if ((strcmp(expr->fn->symbol_name->name, func_table[i].name) == 0) &&
		   (func_table[i].param == -1)) {
		   	*rl = alloc_whole_rl(get_type(expr));
			return 1;
		}
	}
	return 0;
}

static bool state_is_new(struct expression *expr)
{
	struct smatch_state *state;

	state = get_state_expr(my_id, expr);
	if (estate_new(state))
		return true;

	if (expr->type == EXPR_BINOP) {
		if (state_is_new(expr->left))
			return true;
		if (state_is_new(expr->right))
			return true;
	}
	return false;
}

static void handle_derefed_pointers(struct expression *expr, bool is_new)
{
	expr = strip_expr(expr);
	if (expr->type != EXPR_PREOP ||
	    expr->op != '*')
		return;
	expr = strip_expr(expr->unop);
	set_points_to_host_data(expr, is_new);
}

static bool handle_op_assign(struct expression *expr)
{
	struct expression *binop_expr;
	struct smatch_state *state;
	struct range_list *rl;
	bool is_new;

	switch (expr->op) {
	case SPECIAL_ADD_ASSIGN:
	case SPECIAL_SUB_ASSIGN:
	case SPECIAL_AND_ASSIGN:
	case SPECIAL_MOD_ASSIGN:
	case SPECIAL_SHL_ASSIGN:
	case SPECIAL_SHR_ASSIGN:
	case SPECIAL_OR_ASSIGN:
	case SPECIAL_XOR_ASSIGN:
	case SPECIAL_MUL_ASSIGN:
	case SPECIAL_DIV_ASSIGN:
		binop_expr = binop_expression(expr->left,
					      op_remove_assign(expr->op),
					      expr->right);
		if (!get_host_rl(binop_expr, &rl))
			return true;
		rl = cast_rl(get_type(expr->left), rl);
		state = alloc_estate_rl(rl);
		if (expr->op == SPECIAL_AND_ASSIGN ||
		    expr->op == SPECIAL_MOD_ASSIGN ||
		    host_rl_capped(binop_expr))
			estate_set_capped(state);
		is_new = state_is_new(binop_expr);
		if (is_new)
			estate_set_new(state);
		estate_set_assigned(state);
		set_host_data(expr->left, state);
		handle_derefed_pointers(expr->left, is_new);
		return true;
	}
	return false;
}

static struct range_list *strip_negatives(struct range_list *rl)
{
	sval_t min = rl_min(rl);
	sval_t minus_one = { .type = rl_type(rl), .value = -1 };
	sval_t over = { .type = rl_type(rl), .value = INT_MAX + 1ULL };
	sval_t max = sval_type_max(rl_type(rl));

	if (!rl)
		return NULL;

	if (type_unsigned(rl_type(rl)) && type_bits(rl_type(rl)) > 31)
		return remove_range(rl, over, max);

	return remove_range(rl, min, minus_one);
}

static void match_assign_host(struct expression *expr)
{
	struct symbol *left_type, *right_type;
	struct range_list *rl = NULL;
	static struct expression *handled;
	struct smatch_state *state;
	struct expression *faked;
	bool is_capped = false;
	bool is_new = false;

	if (!expr)
		return;

	left_type = get_type(expr->left);
	if (left_type == &void_ctype)
		return;

	faked = get_faked_expression();

	/* FIXME: handle fake array assignments frob(&host_array[x]); */

	if (faked &&
	    faked->type == EXPR_ASSIGNMENT &&
	    points_to_host_data(faked->right)) {
		rl = alloc_whole_rl(get_type(expr->left));
		is_new = true;
		goto set;
	}

	if (faked && faked == handled)
		return;
	if (is_fake_call(expr->right))
		goto clear_old_state;
	if (points_to_host_data(expr->right) &&
	    is_struct_ptr(get_type(expr->left))) {
		handled = expr;
		// This should be handled by smatch_points_to_host_data.c
		//set_points_to_host_data(expr->left);
	}

	if (handle_op_assign(expr))
		return;
	if (expr->op != '=')
		goto clear_old_state;

	/* Handled by DB code */
	if (expr->right->type == EXPR_CALL)
		return;

	if (faked)
		disable_type_val_lookups();
	get_host_rl(expr->right, &rl);
	if (faked)
		enable_type_val_lookups();
	if (!rl)
		goto clear_old_state;

	is_capped = host_rl_capped(expr->right);
	is_new = state_is_new(expr->right);

set:
	right_type = get_type(expr->right);
	if (type_is_ptr(left_type)) {
		if (right_type && right_type->type == SYM_ARRAY)
			set_points_to_host_data(expr->left, is_new);
		return;
	}

	rl = cast_rl(left_type, rl);
	if (is_capped && type_unsigned(right_type) && type_signed(left_type))
		rl = strip_negatives(rl);
	state = alloc_estate_rl(rl);
	if (is_new)
		estate_set_new(state);
	if (is_capped)
		estate_set_capped(state);
	estate_set_assigned(state);
	set_host_data(expr->left, state);
	handle_derefed_pointers(expr->left, is_new);
	return;

clear_old_state:

	/*
	 * HACK ALERT!!!  This should be at the start of the function.  The
	 * the problem is that handling "pointer = array;" assignments is
	 * handled in this function instead of in kernel_points_to_host_data.c.
	 */
	if (type_is_ptr(left_type))
		return;

	if (get_state_expr(my_id, expr->left))
		set_host_data(expr->left, alloc_estate_empty());
}

static void handle_eq_noteq(struct expression *expr)
{
	struct smatch_state *left_orig, *right_orig;

	left_orig = get_state_expr(my_id, expr->left);
	right_orig = get_state_expr(my_id, expr->right);

	if (!left_orig && !right_orig)
		return;
	if (left_orig && right_orig)
		return;

	if (left_orig) {
		set_true_false_states_expr(my_id, expr->left,
				expr->op == SPECIAL_EQUAL ? alloc_estate_empty() : NULL,
				expr->op == SPECIAL_EQUAL ? NULL : alloc_estate_empty());
	} else {
		set_true_false_states_expr(my_id, expr->right,
				expr->op == SPECIAL_EQUAL ? alloc_estate_empty() : NULL,
				expr->op == SPECIAL_EQUAL ? NULL : alloc_estate_empty());
	}
}

static void handle_compare(struct expression *expr)
{
	struct expression  *left, *right;
	struct range_list *left_rl = NULL;
	struct range_list *right_rl = NULL;
	struct range_list *host_rl;
	struct smatch_state *capped_state;
	struct smatch_state *left_true = NULL;
	struct smatch_state *left_false = NULL;
	struct smatch_state *right_true = NULL;
	struct smatch_state *right_false = NULL;
	struct symbol *type;
	sval_t sval;

	left = strip_expr(expr->left);
	right = strip_expr(expr->right);

	while (left->type == EXPR_ASSIGNMENT)
		left = strip_expr(left->left);

	/*
	 * Conditions are mostly handled by smatch_extra.c, but there are some
	 * times where the exact values are not known so we can't do that.
	 *
	 * Normally, we might consider using smatch_capped.c to supliment smatch
	 * extra but that doesn't work when we merge unknown uncapped kernel
	 * data with unknown capped host data.  The result is uncapped host
	 * data.  We need to keep it separate and say that the host data is
	 * capped.  In the past, I would have marked this as just regular
	 * kernel data (not host data) but we can't do that these days because
	 * we need to track host data for Spectre.
	 *
	 * The other situation which we have to handle is when we do have an
	 * int and we compare against an unknown unsigned kernel variable.  In
	 * that situation we assume that the kernel data is less than INT_MAX.
	 * Otherwise then we get all sorts of array underflow false positives.
	 *
	 */

	/* Handled in smatch_extra.c */
	if (get_implied_value(left, &sval) ||
	    get_implied_value(right, &sval))
		return;

	get_host_rl(left, &left_rl);
	get_host_rl(right, &right_rl);

	/* nothing to do */
	if (!left_rl && !right_rl)
		return;
	/* if both sides are host data that's not a good limit */
	if (left_rl && right_rl)
		return;

	if (left_rl)
		host_rl = left_rl;
	else
		host_rl = right_rl;

	type = get_type(expr);
	if (type_unsigned(type))
		host_rl = strip_negatives(host_rl);
	capped_state = alloc_estate_rl(host_rl);
	estate_set_capped(capped_state);

	switch (expr->op) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
	case SPECIAL_LTE:
	case SPECIAL_UNSIGNED_LTE:
		if (left_rl)
			left_true = capped_state;
		else
			right_false = capped_state;
		break;
	case '>':
	case SPECIAL_UNSIGNED_GT:
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GTE:
		if (left_rl)
			left_false = capped_state;
		else
			right_true = capped_state;
		break;
	}
	set_true_false_states_expr(my_id, left, left_true, left_false);
	set_true_false_states_expr(my_id, right, right_true, right_false);
}

static void match_condition_host(struct expression *expr)
{
	if (!expr)
		return;

	if (expr->type != EXPR_COMPARE)
		return;

	if (expr->op == SPECIAL_EQUAL ||
	    expr->op == SPECIAL_NOTEQUAL) {
		handle_eq_noteq(expr);
		return;
	}
	handle_compare(expr);
}

static void match_returns_host_rl(const char *fn, struct expression *expr, void *unused)
{
	func_gets_host_data = true;
}

static int has_host_data(struct symbol *sym)
{
	struct sm_state *tmp;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), tmp) {
		if (tmp->sym == sym)
			return 1;
	} END_FOR_EACH_SM(tmp);
	return 0;
}

bool we_pass_host_data(struct expression *call)
{
	struct expression *arg;
	struct symbol *sym;

	FOR_EACH_PTR(call->args, arg) {
		if (points_to_host_data(arg))
			return true;
		sym = expr_to_sym(arg);
		if (!sym)
			continue;
		if (has_host_data(sym))
			return true;
	} END_FOR_EACH_PTR(arg);

	return false;
}

static int db_returned_host_rl(struct expression *call, struct range_list **rl)
{
	struct smatch_state *state;
	char buf[48];

	if (is_fake_call(call))
		return 0;
	snprintf(buf, sizeof(buf), "return %p", call);
	state = get_state(my_id, buf, NULL);
	if (!state || !estate_rl(state))
		return 0;
	*rl = estate_rl(state);
	return 1;
}

struct stree *get_host_stree(void)
{
	return get_all_states_stree(my_id);
}

static int host_data_flag;
static int no_host_data_flag;

struct range_list *var_host_rl(struct expression *expr)
{
	struct smatch_state *state;
	struct range_list *rl;
	struct range_list *absolute_rl;

	if (expr->type == EXPR_PREOP && expr->op == '&') {
		no_host_data_flag = 1;
		return NULL;
	}

	if (expr->type == EXPR_BINOP && expr->op == '%') {
		struct range_list *left, *right;

		if (!get_host_rl(expr->right, &right))
			return NULL;
		get_absolute_rl(expr->left, &left);
		rl = rl_binop(left, '%', right);
		goto found;
	}

	if (expr->type == EXPR_BINOP && expr->op == '/') {
		struct range_list *left = NULL;
		struct range_list *right = NULL;
		struct range_list *abs_right;

		get_host_rl(expr->left, &left);
		get_host_rl(expr->right, &right);
		get_absolute_rl(expr->right, &abs_right);

		if (left && !right) {
			rl = rl_binop(left, '/', abs_right);
			if (sval_cmp(rl_max(left), rl_max(rl)) < 0)
				no_host_data_flag = 1;
		}

		return NULL;
	}

	if (get_rl_from_function(expr, &rl))
		goto found;

	state = get_state_expr(my_id, expr);
	if (state && estate_rl(state)) {
		rl = estate_rl(state);
		goto found;
	}

	if (expr->type == EXPR_CALL && db_returned_host_rl(expr, &rl))
		goto found;

	if (expr->type == EXPR_PREOP && expr->op == '*' &&
	    points_to_host_data(expr->unop)) {
		rl = var_to_absolute_rl(expr);
		goto found;
	}

	if (is_array(expr)) {
		struct expression *array = get_array_base(expr);

		if (!get_state_expr(my_id, array)) {
			no_host_data_flag = 1;
			return NULL;
		}
	}

	return NULL;
found:
	host_data_flag = 1;
	absolute_rl = var_to_absolute_rl(expr);
	return clone_rl(rl_intersection(rl, absolute_rl));
}

static bool is_ptr_subtract(struct expression *expr)
{
	expr = strip_expr(expr);
	if (!expr)
		return false;
	if (expr->type == EXPR_BINOP && expr->op == '-' &&
	    type_is_ptr(get_type(expr->left))) {
		return true;
	}
	return false;
}

int get_host_rl(struct expression *expr, struct range_list **rl)
{
	if (is_ptr_subtract(expr))
		return 0;

	host_data_flag = 0;
	no_host_data_flag = 0;
	custom_get_absolute_rl(expr, &var_host_rl, rl);
	if (!host_data_flag || no_host_data_flag)
		*rl = NULL;
	return !!*rl;
}

int is_host_rl(struct expression *expr)
{
	struct range_list *tmp;

	return get_host_rl(expr, &tmp) && tmp;
}

int get_host_rl_var_sym(const char *name, struct symbol *sym, struct range_list **rl)
{
	struct smatch_state *state;

	state = get_state(my_id, name, sym);
	if (state && estate_rl(state)) {
		*rl = estate_rl(state);
		return 1;
	}
	return 0;
}

static void return_info_callback_host(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	struct smatch_state *extra;
	struct range_list *rl;
	char buf[64];

	if (is_ignored_kernel_data(printed_name))
		return;

	if (param >= 0) {
		if (strcmp(printed_name, "$") == 0)
			return;
		if (!estate_assigned(sm->state) &&
		    !estate_new(sm->state))
			return;
	}
	rl = estate_rl(sm->state);
	if (!rl)
		return;
	extra = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (estate_rl(extra))
		rl = rl_intersection(estate_rl(sm->state), estate_rl(extra));
	if (!rl)
		return;

	snprintf(buf, sizeof(buf), "%s%s%s",
		 show_rl(rl),
		 estate_capped(sm->state) ? "[c]" : "", "");
	sql_insert_return_states(return_id, return_ranges,
				 estate_new(sm->state) ? HOST_DATA_SET : HOST_DATA,
				 param, printed_name, buf);

}

static void caller_info_callback_host(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	struct smatch_state *state;
	struct range_list *rl;
	struct symbol *type;
	char buf[64];

	/*
	 * Smatch uses a hack where if we get an unsigned long we say it's
	 * both host data and it points to host data.  But if we pass it to a
	 * function which takes an int, then it's just host data.  There's not
	 * enough bytes for it to be a pointer.
	 *
	 */
	type = get_arg_type(call->fn, param);
	if (strcmp(printed_name, "$") != 0 && type && type_bits(type) < type_bits(&ptr_ctype))
		return;

	if (is_ignored_kernel_data(printed_name))
		return;

	if (strcmp(sm->state->name, "") == 0)
		return;

	state = __get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (!state || !estate_rl(state))
		rl = estate_rl(sm->state);
	else
		rl = rl_intersection(estate_rl(sm->state), estate_rl(state));

	if (!rl)
		return;

	snprintf(buf, sizeof(buf), "%s%s%s", show_rl(rl),
		 estate_capped(sm->state) ? "[c]" : "", "");
	sql_insert_caller_info(call, HOST_DATA, param, printed_name, buf);
}

static void db_param_set(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;
	char *name;
	struct symbol *sym;
	struct smatch_state *state;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	state = get_state(my_id, name, sym);
	if (!state)
		goto free;

	set_state(my_id, name, sym, alloc_estate_empty());
free:
	free_string(name);
}

static bool param_data_capped(const char *value)
{
	if (strstr(value, ",c") || strstr(value, "[c"))
		return true;
	return false;
}

static void set_param_host_data(const char *name, struct symbol *sym, char *key, char *value)
{
	struct expression *expr;
	struct range_list *rl = NULL;
	struct smatch_state *state;
	struct symbol *type;
	char *fullname;

	expr = symbol_expression(sym);
	fullname = get_variable_from_key(expr, key, NULL);
	if (!fullname)
		return;

	type = get_member_type_from_key(expr, key);
	if (type && type->type == SYM_STRUCT)
		return;

	if (!type)
		return;

	str_to_rl(type, value, &rl);
	rl = swap_mtag_seed(expr, rl);
	state = alloc_estate_rl(rl);
	if (param_data_capped(value) || is_capped(expr))
		estate_set_capped(state);
	set_state(my_id, fullname, sym, state);
}

#define OLD 0
#define NEW 1

static void store_host_data_return(struct expression *expr, char *key, char *value, bool is_new)
{
	struct smatch_state *state;
	struct range_list *rl;
	struct symbol *type;
	char buf[48];

	if (key[0] != '$')
		return;

	type = get_type(expr);
	snprintf(buf, sizeof(buf), "return %p%s", expr, key + 1);
	call_results_to_rl(expr, type, value, &rl);

	state = alloc_estate_rl(rl);
	if (is_new)
		estate_set_new(state);
	set_state(my_id, buf, NULL, state);
}

static void set_to_host_data(struct expression *expr, char *key, char *value, bool is_new)
{
	struct smatch_state *state;
	char *name;
	struct symbol *sym;
	struct symbol *type;
	struct range_list *rl = NULL;

	type = get_member_type_from_key(expr, key);
	name = get_variable_from_key(expr, key, &sym);
	if (!name || !sym)
		goto free;

	call_results_to_rl(expr, type, value, &rl);

	state = alloc_estate_rl(rl);
	if (param_data_capped(value))
		estate_set_capped(state);
	if (is_new)
		estate_set_new(state);
	estate_set_assigned(state);
	set_state(my_id, name, sym, state);
free:
	free_string(name);
}

static void returns_param_host_data(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;
	struct expression *call;

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (call->type != EXPR_CALL)
		return;

	if (!we_pass_host_data(call))
		return;

	if (param == -1) {
		if (expr->type != EXPR_ASSIGNMENT) {
			store_host_data_return(expr, key, value, OLD);
			return;
		}
		set_to_host_data(expr->left, key, value, OLD);
		return;
	}

	arg = get_argument_from_call_expr(call->args, param);
	if (!arg)
		return;
	set_to_host_data(arg, key, value, OLD);
}

static void returns_param_host_data_set(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;

	func_gets_host_data = true;

	if (param == -1) {
		if (expr->type != EXPR_ASSIGNMENT) {
			store_host_data_return(expr, key, value, NEW);
			return;
		}
		set_to_host_data(expr->left, key, value, NEW);
		return;
	}

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;
	set_to_host_data(arg, key, value, NEW);
}

static void returns_param_capped_host(struct expression *expr, int param, char *key, char *value)
{
	struct smatch_state *state, *new;
	struct symbol *sym;
	char *name;

	name = get_name_sym_from_param_key(expr, param, key, &sym);
	if (!name || !sym)
		goto free;

	state = get_state(my_id, name, sym);
	if (!state || estate_capped(state))
		goto free;

	new = clone_estate(state);
	estate_set_capped(new);

	set_state(my_id, name, sym, new);
free:
	free_string(name);
}

struct param_key_data {
	param_key_hook *call_back;
	int param;
	const char *key;
	void *info;
};

static void match_function_def(struct symbol *sym)
{
	if (is_host_data_fn(sym))
		func_gets_host_data = true;
}

static void set_param_host_input_data(struct expression *expr, const char *name,
				 struct symbol *sym, void *data)
{
	struct expression *arg;

	func_gets_host_data = true;
	arg = gen_expression_from_name_sym(name, sym);
	tag_as_host_data(arg);
}

void register_kernel_host_data(int id)
{
	int i;
	struct host_fn_info *info;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	set_dynamic_states(my_id);
	add_function_data(&func_gets_host_data);
	add_hook(&match_function_def, FUNC_DEF_HOOK);

	add_function_data((unsigned long *)&start_states);
	add_unmatched_state_hook(my_id, &empty_state);
	add_extra_nomod_hook(&extra_nomod_hook);
	add_pre_merge_hook(my_id, &pre_merge_hook);
	add_merge_hook(my_id, &merge_estates);


	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		info = &func_table[i];
		add_function_param_key_hook_late(info->name, &set_param_host_input_data,
						 info->param, info->key, info);
	}

	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		if (func_table[i].param == -1)
			add_function_hook(func_table[i].name, &match_returns_host_rl, NULL);
	}

	add_hook(&match_assign_host, ASSIGNMENT_HOOK);
	select_return_states_hook(PARAM_SET, &db_param_set);
	add_hook(&match_condition_host, CONDITION_HOOK);

	add_caller_info_callback(my_id, caller_info_callback_host);
	add_return_info_callback(my_id, return_info_callback_host);
	select_caller_info_hook(set_param_host_data, HOST_DATA);
	select_return_states_hook(HOST_DATA, &returns_param_host_data);
	select_return_states_hook(HOST_DATA_SET, &returns_param_host_data_set);
	select_return_states_hook(CAPPED_DATA, &returns_param_capped_host);
}
