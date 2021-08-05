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

#include "ctype.h"
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

static void undef(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_id, sm->name, sm->sym, &undefined);
}

char *swap_names(const char *orig, const char *remove, const char *add)
{
	char buf[64];
	int offset, len, ret;
	bool is_addr = false;

	if (add[0] == '&') {
		is_addr = true;
		add++;
	}

	offset = 0;
	while(orig[offset] == '*' || orig[offset] == '&' || orig[offset] == '(')
		offset++;

	len = strlen(remove);
	if (len + offset > strlen(orig))
		return NULL;
	if (orig[offset + len] != '-')
		return NULL;
	if (strncmp(orig + offset, remove, len) != 0)
		return NULL;

	ret = snprintf(buf, sizeof(buf), "%.*s%s%s%s", offset, orig,
		       add, is_addr ? "." : "->", orig + offset + 2 + len);
	if (ret >= sizeof(buf))
		return NULL;
	return alloc_string(buf);
}

static char *swap_with_param(const char *name, struct symbol *sym, struct symbol **sym_p)
{
	struct smatch_state *state;
	struct var_sym *var_sym;
	char *ret;

	/*
	 * Say you know that "foo = bar;" and you have a state "foo->baz" then
	 * we can just substitute "bar" for "foo" giving "bar->baz".
	 */

	if (!sym || !sym->ident)
		return NULL;

	state = get_state(my_id, sym->ident->name, sym);
	if (!state || !state->data)
		return NULL;
	var_sym = state->data;

	ret = swap_names(name, sym->ident->name, var_sym->var);
	if (!ret)
		return NULL;

	*sym_p = var_sym->sym;
	return ret;
}

struct expression *map_container_of_to_simpler_expr_key(struct expression *expr, const char *orig_key, char **new_key)
{
	int offset;
	char *p = (char *)orig_key;
	char *start, *end;
	char buf[64];
	int ret;

	expr = strip_expr(expr);
	if (expr->type != EXPR_DEREF)
		return NULL;

	while (*p != '\0') {
		if (*p == '(' && isdigit(*(p + 1))) {
			start = p;
			offset = strtoul(p + 1, &p, 10);
			if (!p || p[0] != '<' || p[1] != '~' || p[2] != '$' ||
			    p[3] != ')' || p[4] != '-' || p[5] != '>')
				return NULL;
			end = p + 6;
			break;
		}
		p++;
	}
	if (*p == '\0')
		return NULL;

	if (offset != get_member_offset_from_deref(expr))
		return NULL;

	ret = snprintf(buf, sizeof(buf), "%.*s$.%s", (int)(start - orig_key), orig_key, end);
	if (ret >= sizeof(buf))
		return NULL;
	*new_key = alloc_sname(buf);
	return expr->deref;
}

char *get_variable_from_key(struct expression *arg, const char *key, struct symbol **sym)
{
	struct symbol *type;
	char buf[256];
	char *tmp;
	bool address = false;
	int star_cnt = 0;
	bool add_dot = false;
	int ret;

	// FIXME:  this function has been marked for deletion!
	// get_name_sym_from_key()

	if (!arg)
		return NULL;

	arg = strip_expr(arg);

	if (strcmp(key, "$") == 0)
		return expr_to_var_sym(arg, sym);

	if (strcmp(key, "*$") == 0) {
		if (arg->type == EXPR_PREOP && arg->op == '&') {
			arg = strip_expr(arg->unop);
			return expr_to_var_sym(arg, sym);
		} else {
			tmp = expr_to_var_sym(arg, sym);
			if (!tmp)
				return NULL;
			ret = snprintf(buf, sizeof(buf), "*%s", tmp);
			free_string(tmp);
			if (ret >= sizeof(buf))
				return NULL;
			return alloc_string(buf);
		}
	}

	if (strncmp(key, "(*$)", 4) == 0) {
		if (arg->type == EXPR_PREOP && arg->op == '&') {
			arg = strip_expr(arg->unop);
			snprintf(buf, sizeof(buf), "$%s", key + 4);
			return get_variable_from_key(arg, buf, sym);
		} else {
			tmp = expr_to_var_sym(arg, sym);
			if (!tmp)
				return NULL;
			ret = snprintf(buf, sizeof(buf), "(*%s)%s", tmp, key + 4);
			free_string(tmp);
			if (ret >= sizeof(buf))
				return NULL;
			return alloc_string(buf);
		}
	}

	if (strstr(key, "<~$")) {
		struct expression *expr;
		char *new_key;

		expr = map_container_of_to_simpler_expr_key(arg, key, &new_key);
		if (!expr)
			return NULL;
		arg = expr;
		key = new_key;
	}

	while (key[0] == '*') {
		star_cnt++;
		key++;
	}

	if (key[0] == '&') {
		address = true;
		key++;
	}

	/*
	 * FIXME:  This is a hack.
	 * We should be able to parse expressions like (*$)->foo and *$->foo.
	 */
	type = get_type(arg);
	if (is_struct_ptr(type))
		add_dot = true;

	if (arg->type == EXPR_PREOP && arg->op == '&' && star_cnt && !add_dot) {
		arg = strip_expr(arg->unop);
		star_cnt--;
	}

	if (arg->type == EXPR_PREOP && arg->op == '&') {
		arg = strip_expr(arg->unop);
		tmp = expr_to_var_sym(arg, sym);
		if (!tmp)
			return NULL;
		ret = snprintf(buf, sizeof(buf), "%s%.*s%s.%s",
			       address ? "&" : "", star_cnt, "**********",
			       tmp, key + 3);
		if (ret >= sizeof(buf))
			return NULL;
		return alloc_string(buf);
	}

	tmp = expr_to_var_sym(arg, sym);
	if (!tmp)
		return NULL;
	ret = snprintf(buf, sizeof(buf), "%s%.*s%s%s",
		       address ? "&" : "", star_cnt, "**********", tmp, key + 1);
	free_string(tmp);
	if (ret >= sizeof(buf))
		return NULL;
	return alloc_string(buf);
}

static bool split_param_key(char *value, int *param, char *key, int len)
{
	char *p;
	int l, skip = 1;

	l = snprintf(key, len, "%s", value);
	if (l >= len)
		return false;

	p = key;
	while (*p && *p != '$')
		p++;
	if (*p != '$')
		return false;
	p++;

	*param = atoi(p);
	if (*param < 0 || *param > 99)
		return false;

	p++;
	if (*param > 9) {
		skip = 2;
		p++;
	}

	memmove(p - skip, p, l - (p - key) + 1);

	return true;
}

bool get_implied_rl_from_call_str(struct expression *expr, const char *data, struct range_list **rl)
{
	struct smatch_state *state;
	struct expression *arg;
	struct symbol *sym;
	char buf[256];
	char *name;
	int param;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = expr->right;
	if (expr->type != EXPR_CALL)
		return false;

	if (!split_param_key(data, &param, buf, sizeof(buf)))
		return false;

	if (strcmp(buf, "$") == 0) {
		arg = get_argument_from_call_expr(expr->args, param);
		if (!arg)
			return false;
		return get_implied_rl(arg, rl);
	}

	name = get_name_sym_from_key(expr, param, buf, &sym);
	if (!name)
		return false;

	state = get_state(SMATCH_EXTRA, name, sym);
	if (!estate_rl(state))
		return false;
	*rl = estate_rl(state);
	return true;
}

char *get_chunk_from_key(struct expression *arg, char *key, struct symbol **sym, struct var_sym_list **vsl)
{
	*vsl = NULL;

	if (strcmp("$", key) == 0)
		return expr_to_chunk_sym_vsl(arg, sym, vsl);
	return get_variable_from_key(arg, key, sym);
}

static char *state_name_to_param_name(const char *state_name, const char *param_name)
{
	bool address = false;
	int star_cnt = 0;
	int name_len;
	char buf[256];
	int ret;

	/*
	 * Normally what happens is that we map "*foo->bar" to "*param->bar"
	 * but with container_of() there is no notation for that in C and it's
	 * just a Smatch invention.  So in that case, the state name is the
	 * param name.
	 */
	if (strstr(state_name, "<~$"))
		return (char *)state_name;

	name_len = strlen(param_name);

	while (state_name[0] == '*') {
		star_cnt++;
		state_name++;
	}

	if (state_name[0] == '&') {
		address = true;
		state_name++;
	}

	/* ten out of ten stars! */
	if (star_cnt > 10)
		return NULL;

	if (strncmp(state_name, "(*", 2) == 0 &&
	    strncmp(state_name + 2, param_name, name_len) == 0 &&
	    state_name[name_len + 2] == ')') {
		ret = snprintf(buf, sizeof(buf), "%s%.*s(*$)%s",
			       address ? "&" : "",
			       star_cnt, "**********",
			       state_name + name_len + 3);
		if (ret >= sizeof(buf))
			return NULL;
		return alloc_sname(buf);
	}

	if (strcmp(state_name, param_name) == 0) {
		snprintf(buf, sizeof(buf), "%s%.*s$",
			 address ? "&" : "",
			 star_cnt, "**********");
		return alloc_sname(buf);
	}

	/* check for '-' from "->" */
	if (strncmp(state_name, param_name, name_len) == 0 &&
	    state_name[name_len] == '-') {
		ret = snprintf(buf, sizeof(buf), "%s%.*s$%s",
			       address ? "&" : "",
			       star_cnt, "**********",
			       state_name + name_len);
		if (ret >= sizeof(buf))
			return NULL;
		return alloc_sname(buf);
	}
	return NULL;
}

char *get_param_name_var_sym(const char *name, struct symbol *sym)
{
	if (!sym || !sym->ident)
		return NULL;

	return state_name_to_param_name(name, sym->ident->name);
}

const char *get_mtag_name_var_sym(const char *state_name, struct symbol *sym)
{
	struct symbol *type;
	const char *sym_name;
	int name_len;
	static char buf[256];

	/*
	 * mtag_name is different from param_name because mtags can be a struct
	 * instead of a struct pointer.  But we want to treat it like a pointer
	 * because really an mtag is a pointer.  Or in other words, if you pass
	 * a struct foo then you want to talk about foo.bar but with an mtag
	 * you want to refer to it as foo->bar.
	 *
	 */

	if (!sym || !sym->ident)
		return NULL;

	type = get_real_base_type(sym);
	if (type && type->type == SYM_BASETYPE)
		return "*$";

	sym_name = sym->ident->name;
	name_len = strlen(sym_name);

	if (state_name[name_len] == '.' && /* check for '-' from "->" */
	    strncmp(state_name, sym_name, name_len) == 0) {
		snprintf(buf, sizeof(buf), "$->%s", state_name + name_len + 1);
		return buf;
	}

	return state_name_to_param_name(state_name, sym_name);
}

const char *get_mtag_name_expr(struct expression *expr)
{
	char *name;
	struct symbol *sym;
	const char *ret = NULL;

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	ret = get_mtag_name_var_sym(name, sym);
free:
	free_string(name);
	return ret;
}

char *get_param_name(struct sm_state *sm)
{
	return get_param_name_var_sym(sm->name, sm->sym);
}

char *get_param_var_sym_var_sym(const char *name, struct symbol *sym, struct expression *ret_expr, struct symbol **sym_p)
{
	struct smatch_state *state;
	struct var_sym *var_sym;
	int param;

	*sym_p = NULL;

	// FIXME was modified...

	param = get_param_num_from_sym(sym);
	if (param >= 0) {
		*sym_p = sym;
		return alloc_string(name);
	}

	state = get_state(my_id, name, sym);
	if (state && state->data) {
		var_sym = state->data;
		if (!var_sym)
			return NULL;

		*sym_p = var_sym->sym;
		return alloc_string(var_sym->var);
	}

	/* One would think that handling container_of() should be done here
	 * but it it's quite tricky because we only have a name and a sym
	 * and none of the assignments have been handled yet, either here or
	 * in smatch_assignments.c.  On the other hand handling container_of()
	 * in the assignment hook has the advantage that it saves resources and
	 * it should work fine because of the fake assignments which we do.
	 */

	return swap_with_param(name, sym, sym_p);
}

char *get_param_name_sym(struct expression *expr, struct symbol **sym_p)
{
	struct symbol *sym;
	const char *ret = NULL;
	char *name;

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	ret = get_param_var_sym_var_sym(name, sym, NULL, sym_p);
free:
	free_string(name);
	return alloc_string(ret);
}

int get_param_key_from_var_sym(const char *name, struct symbol *sym,
			       struct expression *ret_expr,
			       const char **key)
{
	const char *param_name;
	char *other_name;
	struct symbol *other_sym;
	int param;

	if (key)
		*key = name;

	/* straight forward param match */
	param = get_param_num_from_sym(sym);
	if (param >= 0) {
		param_name = get_param_name_var_sym(name, sym);
		if (param_name) {
			if (key)
				*key = param_name;
			return param;
		}
	}

	/* it's the return value */
	if (ret_expr) {
		struct symbol *ret_sym;
		char *ret_str;

		ret_str = expr_to_str_sym(ret_expr, &ret_sym);
		if (ret_str && ret_sym == sym) {
			param_name = state_name_to_param_name(name, ret_str);
			if (param_name) {
				free_string(ret_str);
				if (key)
					*key = param_name;
				return -1;
			}
		}
		free_string(ret_str);
	}

	other_name = get_param_var_sym_var_sym(name, sym, ret_expr, &other_sym);
	if (!other_name || !other_sym)
		return -2;
	param = get_param_num_from_sym(other_sym);
	if (param < 0) {
		sm_msg("internal: '%s' parameter not found", other_name);
		return -2;
	}

	param_name = get_param_name_var_sym(other_name, other_sym);
	if (param_name) {
		if (key)
			*key = param_name;
		return param;
	}
	return -2;
}

int get_param_key_from_sm(struct sm_state *sm, struct expression *ret_expr,
			  const char **key)
{
	return get_param_key_from_var_sym(sm->name, sm->sym, ret_expr, key);
}

int map_to_param(const char *name, struct symbol *sym)
{
	return get_param_key_from_var_sym(name, sym, NULL, NULL);
}

int get_param_num_from_sym(struct symbol *sym)
{
	struct symbol *tmp;
	int i;

	if (!sym)
		return UNKNOWN_SCOPE;

	if (sym->ctype.modifiers & MOD_TOPLEVEL) {
		if (sym->ctype.modifiers & MOD_STATIC)
			return FILE_SCOPE;
		return GLOBAL_SCOPE;
	}

	if (!cur_func_sym) {
		if (!parse_error) {
			sm_msg("warn: internal.  problem with scope:  %s",
			       sym->ident ? sym->ident->name : "<anon var>");
		}
		return GLOBAL_SCOPE;
	}


	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, tmp) {
		if (tmp == sym)
			return i;
		i++;
	} END_FOR_EACH_PTR(tmp);
	return LOCAL_SCOPE;
}

int get_param_num(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	if (!cur_func_sym)
		return UNKNOWN_SCOPE;
	name = expr_to_var_sym(expr, &sym);
	free_string(name);
	if (!sym)
		return UNKNOWN_SCOPE;
	return get_param_num_from_sym(sym);
}

struct symbol *get_param_sym_from_num(int num)
{
	struct symbol *sym;
	int i;

	if (!cur_func_sym)
		return NULL;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, sym) {
		if (i++ == num)
			return sym;
	} END_FOR_EACH_PTR(sym);
	return NULL;
}

char *get_name_sym_from_key(struct expression *expr, int param, const char *key, struct symbol **sym)
{
	struct expression *call, *arg;
	char *name;

	if (sym)
		*sym = NULL;

	if (!expr) {
		sm_msg("internal: null call_expr.  param=%d key='%s'", param, key);
		return NULL;
	}

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);

	if (call->type != EXPR_CALL)
		return NULL;

	if (param == -1 &&
	    expr->type == EXPR_ASSIGNMENT &&
	    expr->op == '=') {
		name = get_variable_from_key(expr->left, key, sym);
		if (!name || (sym && !*sym))
			goto free;
	} else if (param >= 0) {
		arg = get_argument_from_call_expr(call->args, param);
		if (!arg)
			return NULL;

		name = get_variable_from_key(arg, key, sym);
		if (!name || (sym && !*sym))
			goto free;
	} else {
		name = alloc_string(key);
	}

	return name;
free:
	free_string(name);
	return NULL;
}

char *handle_container_of_assign(struct expression *expr, struct symbol **sym)
{
	struct expression *right, *orig;
	struct symbol *type;
	sval_t sval;
	int param;
	char buf[64];

	type = get_type(expr->left);
	if (!type || type->type != SYM_PTR)
		return NULL;

	right = strip_expr(expr->right);
	if (right->type != EXPR_BINOP || right->op != '-')
		return NULL;

	if (!get_value(right->right, &sval) ||
	   sval.value < 0 || sval.value > MTAG_OFFSET_MASK)
		return NULL;

	orig = get_assigned_expr(right->left);
	if (!orig)
		return NULL;
	if (orig->type != EXPR_SYMBOL)
		return NULL;
	param = get_param_num_from_sym(orig->symbol);
	if (param < 0)
		return NULL;

	snprintf(buf, sizeof(buf), "(%lld<~$)", sval.value);
	*sym = orig->symbol;
	return alloc_string(buf);
}

const char *get_container_of_str(struct expression *expr)
{
	struct smatch_state *state;

	state = get_state_expr(my_id, expr);
	if (!state)
		return NULL;
	return state->name;
}

static void match_assign(struct expression *expr)
{
	struct symbol *param_sym;
	char *param_name;

	if (expr->op != '=')
		return;

	/* __in_fake_parameter_assign is included deliberately */
	if (is_fake_call(expr->right) ||
	    __in_fake_struct_assign)
		return;

	param_name = get_param_name_sym(expr->right, &param_sym);
	if (param_name && param_sym)
		goto set_state;

	param_name = handle_container_of_assign(expr, &param_sym);
	if (param_name && param_sym)
		goto set_state;

	goto free;

set_state:
	set_state_expr(my_id, expr->left, alloc_var_sym_state(param_name, param_sym));
free:
	free_string(param_name);
}

void register_param_key(int id)
{
	my_id = id;

	set_dynamic_states(my_id);
	add_hook(&match_assign, ASSIGNMENT_HOOK_AFTER);
	add_modification_hook(my_id, &undef);
}

