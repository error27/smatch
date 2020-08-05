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
static bool func_gets_user_data;

static const char *kstr_funcs[] = {
	"kstrtoull", "kstrtoll", "kstrtoul", "kstrtol", "kstrtouint",
	"kstrtoint", "kstrtou64", "kstrtos64", "kstrtou32", "kstrtos32",
	"kstrtou16", "kstrtos16", "kstrtou8", "kstrtos8", "kstrtoull_from_user"
	"kstrtoll_from_user", "kstrtoul_from_user", "kstrtol_from_user",
	"kstrtouint_from_user", "kstrtoint_from_user", "kstrtou16_from_user",
	"kstrtos16_from_user", "kstrtou8_from_user", "kstrtos8_from_user",
	"kstrtou64_from_user", "kstrtos64_from_user", "kstrtou32_from_user",
	"kstrtos32_from_user",
};

static const char *returns_user_data[] = {
	"simple_strtol", "simple_strtoll", "simple_strtoul", "simple_strtoull",
	"kvm_register_read",
};

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
	struct smatch_state *user = cur->state;
	struct smatch_state *extra;
	struct smatch_state *state;
	struct range_list *rl;

	extra = __get_state(SMATCH_EXTRA, cur->name, cur->sym);
	if (!extra)
		return;
	rl = rl_intersection(estate_rl(user), estate_rl(extra));
	state = alloc_estate_rl(clone_rl(rl));
	if (estate_capped(user) || is_capped_var_sym(cur->name, cur->sym))
		estate_set_capped(state);
	if (estate_treat_untagged(user))
		estate_set_treat_untagged(state);
	if (estates_equiv(state, cur->state))
		return;
	set_state(my_id, cur->name, cur->sym, state);
}

static void extra_nomod_hook(const char *name, struct symbol *sym, struct expression *expr, struct smatch_state *state)
{
	struct smatch_state *user, *new;
	struct range_list *rl;

	user = __get_state(my_id, name, sym);
	if (!user)
		return;
	rl = rl_intersection(estate_rl(user), estate_rl(state));
	if (rl_equiv(rl, estate_rl(user)))
		return;
	new = alloc_estate_rl(rl);
	if (estate_capped(user))
		estate_set_capped(new);
	if (estate_treat_untagged(user))
		estate_set_treat_untagged(new);
	set_state(my_id, name, sym, new);
}

static bool user_rl_known(struct expression *expr)
{
	struct range_list *rl;
	sval_t close_to_max;

	if (!get_user_rl(expr, &rl))
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

	if (expr->op == '-' && get_user_rl(expr->left, &left_rl)) {
		if (user_rl_capped(expr->left))
			return true;
		comparison = get_comparison(expr->left, expr->right);
		if (comparison && show_special(comparison)[0] == '>')
			return true;
		return false;
	}

	if (expr->op == '&' || expr->op == '%') {
		bool left_user, left_capped, right_user, right_capped;

		if (!get_value(expr->right, &sval) && is_capped(expr->right))
			return true;
		if (is_array_index_mask_nospec(expr->right))
			return true;
		if (is_capped(expr->left))
			return true;
		left_user = is_user_rl(expr->left);
		right_user = is_user_rl(expr->right);
		if (!left_user && !right_user)
			return true;

		left_capped = user_rl_capped(expr->left);
		right_capped = user_rl_capped(expr->right);

		if (left_user && left_capped) {
			if (!right_user)
				return true;
			if (right_user && right_capped)
				return true;
			return false;
		}
		if (right_user && right_capped) {
			if (!left_user)
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
	 * plus a user variable which is clamped to a known range (uncapped)
	 * the result should be capped.
	 */
	if ((user_rl_capped(expr->left) || user_rl_known(expr->left)) &&
	    (user_rl_capped(expr->right) || user_rl_known(expr->right)))
		return true;

	return false;
}

bool user_rl_capped(struct expression *expr)
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
		return user_rl_capped(expr->unop);
	state = get_state_expr(my_id, expr);
	if (state)
		return estate_capped(state);

	if (!get_user_rl(expr, &rl)) {
		/*
		 * The non user data parts of a binop are capped and
		 * also empty user rl states are capped.
		 */
		return true;
	}

	if (rl_to_sval(rl, &sval))
		return true;

	return false;  /* uncapped user data */
}

bool user_rl_treat_untagged(struct expression *expr)
{
	struct smatch_state *state;
	struct range_list *rl;
	sval_t sval;

	expr = strip_expr(expr);
	if (!expr)
		return false;
	if (get_value(expr, &sval))
		return true;

	state = get_state_expr(my_id, expr);
	if (state)
		return estate_treat_untagged(state);

	if (get_user_rl(expr, &rl))
		return false;  /* uncapped user data */

	return true;  /* not actually user data */
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
		set_state_expr(my_id, edge_member, new_state(type));
	} END_FOR_EACH_PTR(tmp);
}

void __set_user_string(struct expression *expr);
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
			set_points_to_user_data(member);
		} else {
			set_state_expr(my_id, member, new_state(get_type(member)));
		}
	} END_FOR_EACH_PTR(tmp);
}

static void tag_base_type(struct expression *expr)
{
	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = strip_expr(expr->unop);
	else
		expr = deref_expression(expr);
	set_state_expr(my_id, expr, new_state(get_type(expr)));
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
		set_state_expr(my_id, deref_expression(expr), new_state(&ulong_ctype));
		return;
	}
	if (type->type == SYM_BASETYPE) {
		if (expr->type != EXPR_PREOP && expr->op != '&')
			set_points_to_user_data(expr);
		tag_base_type(expr);
		return;
	}
	if (type->type == SYM_STRUCT || type->type == SYM_UNION) {
		if (expr->type != EXPR_PREOP || expr->op != '&')
			expr = deref_expression(expr);
		else
			set_state_expr(my_id, deref_expression(expr), new_state(&ulong_ctype));
		tag_struct_members(type, expr);
	}
}

static void match_user_copy(const char *fn, struct expression *expr, void *_param)
{
	int param = PTR_INT(_param);
	struct expression *dest;

	func_gets_user_data = true;

	dest = get_argument_from_call_expr(expr->args, param);
	dest = strip_expr(dest);
	if (!dest)
		return;
	tag_as_user_data(dest);
}

static int is_dev_attr_name(struct expression *expr)
{
	char *name;
	int ret = 0;

	name = expr_to_str(expr);
	if (!name)
		return 0;
	if (strstr(name, "->attr.name"))
		ret = 1;
	free_string(name);
	return ret;
}

static int ends_in_n(struct expression *expr)
{
	struct string *str;

	if (!expr)
		return 0;
	if (expr->type != EXPR_STRING || !expr->string)
		return 0;

	str = expr->string;
	if (str->length < 3)
		return 0;

	if (str->data[str->length - 3] == '%' &&
	    str->data[str->length - 2] == 'n')
		return 1;
	return 0;
}

static void match_sscanf(const char *fn, struct expression *expr, void *unused)
{
	struct expression *str, *format, *arg;
	int i, last;

	func_gets_user_data = true;

	str = get_argument_from_call_expr(expr->args, 0);
	if (is_dev_attr_name(str))
		return;

	format = get_argument_from_call_expr(expr->args, 1);
	if (is_dev_attr_name(format))
		return;

	last = ptr_list_size((struct ptr_list *)expr->args) - 1;

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		i++;
		if (i < 2)
			continue;
		if (i == last && ends_in_n(format))
			continue;
		tag_as_user_data(arg);
	} END_FOR_EACH_PTR(arg);
}

static int get_rl_from_function(struct expression *expr, struct range_list **rl)
{
	int i;

	if (expr->type != EXPR_CALL || expr->fn->type != EXPR_SYMBOL ||
	    !expr->fn->symbol_name || !expr->fn->symbol_name->name)
		return 0;

	for (i = 0; i < ARRAY_SIZE(returns_user_data); i++) {
		if (strcmp(expr->fn->symbol_name->name, returns_user_data[i]) == 0) {
			*rl = alloc_whole_rl(get_type(expr));
			return 1;
		}
	}
	return 0;
}

static int comes_from_skb_data(struct expression *expr)
{
	expr = strip_expr(expr);
	if (!expr || expr->type != EXPR_PREOP || expr->op != '*')
		return 0;

	expr = strip_expr(expr->unop);
	if (!expr)
		return 0;
	if (expr->type == EXPR_BINOP && expr->op == '+')
		expr = strip_expr(expr->left);

	return is_skb_data(expr);
}

static int handle_get_user(struct expression *expr)
{
	char *name;
	int ret = 0;

	name = get_macro_name(expr->pos);
	if (!name || strcmp(name, "get_user") != 0)
		return 0;

	name = expr_to_var(expr->right);
	if (!name || (strcmp(name, "__val_gu") != 0 && strcmp(name, "__gu_val") != 0))
		goto free;
	set_state_expr(my_id, expr->left, new_state(get_type(expr->left)));
	ret = 1;
free:
	free_string(name);
	return ret;
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

static bool handle_op_assign(struct expression *expr)
{
	struct expression *binop_expr;
	struct smatch_state *state;
	struct range_list *rl;

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
		if (!get_user_rl(binop_expr, &rl))
			return true;

		rl = cast_rl(get_type(expr->left), rl);
		state = alloc_estate_rl(rl);
		if (user_rl_capped(binop_expr))
			estate_set_capped(state);
		if (user_rl_treat_untagged(expr->left))
			estate_set_treat_untagged(state);
		if (state_is_new(binop_expr))
			estate_set_new(state);
		set_state_expr(my_id, expr->left, state);
		return true;
	}
	return false;
}

static void match_assign(struct expression *expr)
{
	struct symbol *left_type, *right_type;
	struct range_list *rl;
	static struct expression *handled;
	static struct expression *ignore;
	struct smatch_state *state;
	struct expression *faked;
	bool is_capped = false;
	bool is_new = false;

	left_type = get_type(expr->left);
	if (left_type == &void_ctype)
		return;

	faked = get_faked_expression();
	if (0 && ignore && faked == ignore) {
		if (local_debug)
			sm_msg("%s: ignored = '%s'", __func__, expr_to_str(faked));
		return;
	}

	/* FIXME: handle fake array assignments frob(&user_array[x]); */

	if (is_fake_call(expr->right) && faked &&
	    faked->type == EXPR_ASSIGNMENT &&
	    points_to_user_data(faked->right)) {
		if (is_skb_data(faked->right))
			func_gets_user_data = true;
		rl = alloc_whole_rl(get_type(expr->left));
		is_new = true;
		goto set;
	}

	if (local_debug)
		sm_msg("%s: expr = '%s'", __func__, expr_to_str(expr));

	if (faked && faked == handled)
		return;
	if (is_fake_call(expr->right))
		goto clear_old_state;
	if (handle_get_user(expr))
		return;
	if (points_to_user_data(expr->right) &&
	    is_struct_ptr(get_type(expr->left))) {
		handled = expr;
		// This should be handled by smatch_points_to_user_data.c
		// set_points_to_user_data(expr->left);
	}

	if (handle_op_assign(expr))
		return;
	if (expr->op != '=')
		goto clear_old_state;

	/* Handled by DB code */
	if (expr->right->type == EXPR_CALL || __in_fake_parameter_assign)
		return;

	if (!get_user_rl(expr->right, &rl))
		goto clear_old_state;
	is_capped = user_rl_capped(expr->right);
	is_new = state_is_new(expr->right);

set:
	if (type_is_ptr(left_type)) {
		right_type = get_type(expr->right);
		if (right_type && right_type->type == SYM_ARRAY)
			set_points_to_user_data(expr->left);
		if (faked)
			ignore = faked;
		else
			ignore = expr;
		return;
	}

	rl = cast_rl(left_type, rl);
	state = alloc_estate_rl(rl);
	if (is_new)
		estate_set_new(state);
	if (is_capped)
		estate_set_capped(state);
	if (user_rl_treat_untagged(expr->right))
		estate_set_treat_untagged(state);

	if (local_debug)
		sm_msg("%s: left = '%s' user_state = '%s'", __func__, expr_to_str(expr->left), state->name);
	set_state_expr(my_id, expr->left, state);

	return;

clear_old_state:

	/*
	 * HACK ALERT!!!  This should be at the start of the function.  The
	 * the problem is that handling "pointer = array;" assignments is
	 * handled in this function instead of in kernel_points_to_user_data.c.
	 */
	if (type_is_ptr(left_type)) {
		if (faked)
			ignore = faked;
		else
			ignore = expr;
		return;
	}

	if (get_state_expr(my_id, expr->left))
		set_state_expr(my_id, expr->left, alloc_estate_empty());
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

static void handle_compare(struct expression *expr)
{
	struct expression  *left, *right;
	struct range_list *left_rl = NULL;
	struct range_list *right_rl = NULL;
	struct range_list *user_rl;
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
	 * data with unknown capped user data.  The result is uncapped user
	 * data.  We need to keep it separate and say that the user data is
	 * capped.  In the past, I would have marked this as just regular
	 * kernel data (not user data) but we can't do that these days because
	 * we need to track user data for Spectre.
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

	get_user_rl(left, &left_rl);
	get_user_rl(right, &right_rl);

	/* nothing to do */
	if (!left_rl && !right_rl)
		return;
	/* if both sides are user data that's not a good limit */
	if (left_rl && right_rl)
		return;

	if (left_rl)
		user_rl = left_rl;
	else
		user_rl = right_rl;

	type = get_type(expr);
	if (type_unsigned(type))
		user_rl = strip_negatives(user_rl);
	capped_state = alloc_estate_rl(user_rl);
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

static void match_condition(struct expression *expr)
{
	if (expr->type != EXPR_COMPARE)
		return;

	if (expr->op == SPECIAL_EQUAL ||
	    expr->op == SPECIAL_NOTEQUAL) {
		handle_eq_noteq(expr);
		return;
	}

	handle_compare(expr);
}

static void match_returns_user_rl(const char *fn, struct expression *expr, void *unused)
{
	func_gets_user_data = true;
}

static int get_user_macro_rl(struct expression *expr, struct range_list **rl)
{
	struct expression *parent;
	char *macro;

	if (!expr)
		return 0;

	macro = get_macro_name(expr->pos);
	if (!macro)
		return 0;

	/* handle ntohl(foo[i]) where "i" is trusted */
	parent = expr_get_parent_expr(expr);
	while (parent && parent->type != EXPR_BINOP)
		parent = expr_get_parent_expr(parent);
	if (parent && parent->type == EXPR_BINOP) {
		char *parent_macro = get_macro_name(parent->pos);

		if (parent_macro && strcmp(macro, parent_macro) == 0)
			return 0;
	}

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

static int has_user_data(struct symbol *sym)
{
	struct sm_state *tmp;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), tmp) {
		if (tmp->sym == sym)
			return 1;
	} END_FOR_EACH_SM(tmp);
	return 0;
}

bool we_pass_user_data(struct expression *call)
{
	struct expression *arg;
	struct symbol *sym;

	FOR_EACH_PTR(call->args, arg) {
		if (local_debug)
			sm_msg("%s: arg = '%s' %s", __func__,
			       expr_to_str(arg),
			       points_to_user_data(arg) ? "user pointer" : "not");
		if (points_to_user_data(arg))
			return true;
		sym = expr_to_sym(arg);
		if (!sym)
			continue;
		if (has_user_data(sym))
			return true;
	} END_FOR_EACH_PTR(arg);

	return false;
}

static int db_returned_user_rl(struct expression *call, struct range_list **rl)
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

struct stree *get_user_stree(void)
{
	return get_all_states_stree(my_id);
}

static int user_data_flag;
static int no_user_data_flag;
struct range_list *var_user_rl(struct expression *expr)
{
	struct smatch_state *state;
	struct range_list *rl;
	struct range_list *absolute_rl;

	if (expr->type == EXPR_PREOP && expr->op == '&') {
		no_user_data_flag = 1;
		return NULL;
	}

	if (expr->type == EXPR_BINOP && expr->op == '%') {
		struct range_list *left, *right;

		if (!get_user_rl(expr->right, &right))
			return NULL;
		get_absolute_rl(expr->left, &left);
		rl = rl_binop(left, '%', right);
		goto found;
	}

	if (expr->type == EXPR_BINOP && expr->op == '/') {
		struct range_list *left = NULL;
		struct range_list *right = NULL;
		struct range_list *abs_right;

		/*
		 * The specific bug I'm dealing with is:
		 *
		 * foo = capped_user / unknown;
		 *
		 * Instead of just saying foo is now entirely user_rl we should
		 * probably say instead that it is not at all user data.
		 *
		 */

		get_user_rl(expr->left, &left);
		get_user_rl(expr->right, &right);
		get_absolute_rl(expr->right, &abs_right);

		if (left && !right) {
			rl = rl_binop(left, '/', abs_right);
			if (sval_cmp(rl_max(left), rl_max(rl)) < 0)
				no_user_data_flag = 1;
		}

		return NULL;
	}

	if (get_rl_from_function(expr, &rl))
		goto found;

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

	if (expr->type == EXPR_CALL && db_returned_user_rl(expr, &rl))
		goto found;

	if (expr->type == EXPR_PREOP && expr->op == '*' &&
	    points_to_user_data(expr->unop)) {
		rl = var_to_absolute_rl(expr);
		goto found;
	}

	if (is_array(expr)) {
		struct expression *array = get_array_base(expr);

		if (!get_state_expr(my_id, array)) {
			no_user_data_flag = 1;
			return NULL;
		}
	}

	return NULL;
found:
	user_data_flag = 1;
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

int get_user_rl(struct expression *expr, struct range_list **rl)
{
	if (is_ptr_subtract(expr))
		return 0;

	user_data_flag = 0;
	no_user_data_flag = 0;
	custom_get_absolute_rl(expr, &var_user_rl, rl);
	if (!user_data_flag || no_user_data_flag)
		*rl = NULL;

	return !!*rl;
}

int is_user_rl(struct expression *expr)
{
	struct range_list *tmp;

	return get_user_rl(expr, &tmp) && tmp;
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

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	struct smatch_state *extra;
	struct range_list *rl;
	char buf[64];

	if (param >= 0) {
		if (strcmp(printed_name, "$") == 0)
			return;
		if (!param_was_set_var_sym(sm->name, sm->sym))
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
		 estate_capped(sm->state) ? "[c]" : "",
		 estate_treat_untagged(sm->state) ? "[u]" : "");
	sql_insert_return_states(return_id, return_ranges,
				 estate_new(sm->state) ? USER_DATA_SET : USER_DATA,
				 param, printed_name, buf);
}

static void caller_info_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	struct smatch_state *state;
	struct range_list *rl;
	struct symbol *type;
	char buf[64];

	if (local_debug)
		sm_msg("%s: name = '%s' sm = '%s'", __func__, printed_name, show_sm(sm));

	/*
	 * Smatch uses a hack where if we get an unsigned long we say it's
	 * both user data and it points to user data.  But if we pass it to a
	 * function which takes an int, then it's just user data.  There's not
	 * enough bytes for it to be a pointer.
	 *
	 */
	type = get_arg_type(call->fn, param);
	if (strcmp(printed_name, "$") != 0 && type && type_bits(type) < type_bits(&ptr_ctype))
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
		 estate_capped(sm->state) ? "[c]" : "",
		 estate_treat_untagged(sm->state) ? "[u]" : "");
	sql_insert_caller_info(call, USER_DATA, param, printed_name, buf);
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

static bool param_data_treat_untagged(const char *value)
{
	if (strstr(value, ",u") || strstr(value, "[u"))
		return true;
	return false;
}

static void set_param_user_data(const char *name, struct symbol *sym, char *key, char *value)
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
	if (type && type->type == SYM_STRUCT) {
		sm_info("%s: user data struct.  key='%s' value='%s'",
		        __func__, key, value);
		return;
	}

	// FIXME: This is temporary.  Just run this on Thursday and then
	// let's make it a printf() and then delete it.
	if (!type) {
		sm_msg("%s: no type for '%s'", __func__, fullname);
		return;
	}

	str_to_rl(type, value, &rl);
	rl = swap_mtag_seed(expr, rl);
	state = alloc_estate_rl(rl);
	if (param_data_capped(value) || is_capped(expr))
		estate_set_capped(state);
	if (param_data_treat_untagged(value) || sym->ctype.as == 5)
		estate_set_treat_untagged(state);
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

#define OLD 0
#define NEW 1

static void store_user_data_return(struct expression *expr, char *key, char *value, bool is_new)
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

static void set_to_user_data(struct expression *expr, char *key, char *value, bool is_new)
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
	if (param_data_treat_untagged(value))
		estate_set_treat_untagged(state);
	if (is_new)
		estate_set_new(state);
	set_state(my_id, name, sym, state);
free:
	free_string(name);
}

static void returns_param_user_data(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;
	struct expression *call;

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (call->type != EXPR_CALL)
		return;

	if (!we_pass_user_data(call))
		return;

	if (param == -1) {
		if (expr->type != EXPR_ASSIGNMENT) {
			store_user_data_return(expr, key, value, OLD);
			return;
		}
		set_to_user_data(expr->left, key, value, OLD);
		return;
	}

	arg = get_argument_from_call_expr(call->args, param);
	if (!arg)
		return;
	set_to_user_data(arg, key, value, OLD);
}

static void returns_param_user_data_set(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;

	func_gets_user_data = true;

	if (param == -1) {
		if (expr->type != EXPR_ASSIGNMENT) {
			store_user_data_return(expr, key, value, NEW);
			return;
		}
		set_to_user_data(expr->left, key, value, NEW);
		return;
	}

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;
	set_to_user_data(arg, key, value, NEW);
}

static void returns_param_capped(struct expression *expr, int param, char *key, char *value)
{
	struct smatch_state *state, *new;
	struct symbol *sym;
	char *name;

	name = return_state_to_var_sym(expr, param, key, &sym);
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

static struct int_stack *gets_data_stack;
static void match_function_def(struct symbol *sym)
{
	func_gets_user_data = false;

	if (is_user_data_fn(sym))
		func_gets_user_data = true;
}

static void match_inline_start(struct expression *expr)
{
	push_int(&gets_data_stack, func_gets_user_data);
}

static void match_inline_end(struct expression *expr)
{
	func_gets_user_data = pop_int(&gets_data_stack);
}

void register_kernel_user_data(int id)
{
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	set_dynamic_states(my_id);

	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_inline_start, INLINE_FN_START);
	add_hook(&match_inline_end, INLINE_FN_END);

	add_hook(&save_start_states, AFTER_DEF_HOOK);
	add_hook(&free_start_states, AFTER_FUNC_HOOK);
	add_hook(&match_save_states, INLINE_FN_START);
	add_hook(&match_restore_states, INLINE_FN_END);

	add_unmatched_state_hook(my_id, &empty_state);
	add_extra_nomod_hook(&extra_nomod_hook);
	add_pre_merge_hook(my_id, &pre_merge_hook);
	add_merge_hook(my_id, &merge_estates);

	add_function_hook("copy_from_user", &match_user_copy, INT_PTR(0));
	add_function_hook("__copy_from_user", &match_user_copy, INT_PTR(0));
	add_function_hook("memcpy_fromiovec", &match_user_copy, INT_PTR(0));
	for (i = 0; i < ARRAY_SIZE(kstr_funcs); i++)
		add_function_hook(kstr_funcs[i], &match_user_copy, INT_PTR(2));
	add_function_hook("usb_control_msg", &match_user_copy, INT_PTR(6));

	for (i = 0; i < ARRAY_SIZE(returns_user_data); i++)
		add_function_hook(returns_user_data[i], &match_returns_user_rl, NULL);

	add_function_hook("sscanf", &match_sscanf, NULL);

	add_hook(&match_syscall_definition, AFTER_DEF_HOOK);

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	select_return_states_hook(PARAM_SET, &db_param_set);
	add_hook(&match_condition, CONDITION_HOOK);

	add_caller_info_callback(my_id, caller_info_callback);
	add_return_info_callback(my_id, return_info_callback);
	select_caller_info_hook(set_param_user_data, USER_DATA);
	select_return_states_hook(USER_DATA, &returns_param_user_data);
	select_return_states_hook(USER_DATA_SET, &returns_param_user_data_set);
	select_return_states_hook(CAPPED_DATA, &returns_param_capped);
}

void register_kernel_user_data2(int id)
{
	my_call_id = id;

	if (option_project != PROJ_KERNEL)
		return;
	select_caller_info_hook(set_called, INTERNAL);
}

