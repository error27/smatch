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

/*
 * The param/key stuff is changes "0/$->foo" into a name/sym pair.
 * It also handles more complicated mappings like container_of(param).
 * The problem with the current implementation of the container_of()
 * code in 2023 is that it does stuff like (1624<~$0)->sk.sk_write_queue.next
 * where 1624 is the number of bytes.  It's difficult to write that kind
 * of code by hand.
 *
 * We also need to be able to handle things like netdev_priv().  When you
 * allocate a netdev struct then you have to specify how much space you need
 * for your private data and it allocates a enough data to hold everything.
 * the netdev_priv() function returns a pointer to beyond the end of the
 * netdev struct.  So the container_of() macro subtracts and the netdev_priv()
 * function adds.
 *
 */

#include "ctype.h"
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

char *swap_names(const char *orig, const char *remove, const char *add)
{
	char buf[64];
	int offset, len, ret;
	bool is_addr = false;
	bool is_star = false;  /* fixme: this should be star_cnt */
	bool is_end = false;

	if (orig[0] == '*')
		is_star = true;

	if (add[0] == '&') {
		if (is_star)
			is_star = false;
		else
			is_addr = true;
		add++;
	}

	offset = 0;
	while(orig[offset] == '*' || orig[offset] == '&' || orig[offset] == '(')
		offset++;

	len = strlen(remove);
	if (len + offset > strlen(orig))
		return NULL;
	if (orig[offset + len] == '\0')
		is_end = true;
	else if (orig[offset + len] != '-')
		return NULL;
	if (strncmp(orig + offset, remove, len) != 0)
		return NULL;

	if (!is_star && is_end)
		return NULL;

	ret = snprintf(buf, sizeof(buf), "%.*s%s%s%s", offset, orig,
		       add,
		       is_end ? "" : (is_addr ? "." : "->"),
		       is_end ? "" : orig + offset + 2 + len);
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
	struct expression *container;
	int offset = -1;
	char *p = (char *)orig_key;
	char buf[64];
	char *start;
	int param;
	int ret;
	bool arrow = false;
	bool no_member = false;

	expr = strip_expr(expr);
	if (expr->type != EXPR_DEREF &&
	    (expr->type != EXPR_PREOP && expr->op == '&'))
		return NULL;

	while (*p != '\0') {
		if (*p == '(' && isdigit(*(p + 1))) {
			start = p;
			offset = strtoul(p + 1, &p, 10);
			if (!p || strncmp(p, "<~$", 3) != 0)
				return NULL;
			p += 3;
			if (!isdigit(p[0]))
				return NULL;
			param = strtoul(p + 1, &p, 10);
			/* fixme */
			if (param != 0)
				return NULL;
			if (!p)
				return NULL;
			if (strcmp(p, ")") == 0) {
				no_member = true;
				p++;
				break;
			}
			if (strncmp(p, ")->", 3) != 0)
				return NULL;
			p += 3;
			break;
		}
		p++;
	}
	if (!no_member && *p == '\0')
		return NULL;

	if (offset == get_member_offset_from_deref(expr)) {
		if (expr->type == EXPR_PREOP && expr->op == '&') {
			expr = strip_expr(expr->unop);
			if (expr->type != EXPR_DEREF)
				return NULL;
			expr = strip_expr(expr->deref);
			if (expr->type != EXPR_PREOP || expr->op != '*')
				return NULL;
			container = expr->unop;
			arrow = true;
		}
		container = expr->deref;
	} else {
		container = get_stored_container(expr, offset);
		if (!container)
			return NULL;
		arrow = true;
	}

	if (no_member) {
		*new_key = alloc_sname("$");
		return container;
	}

	ret = snprintf(buf, sizeof(buf), "%.*s$%s%s", (int)(start - orig_key), orig_key, arrow ? "->" : ".", p);
	if (ret >= sizeof(buf))
		return NULL;
	*new_key = alloc_sname(buf);

	return container;
}

struct expression *map_netdev_priv_to_simpler_expr_key(struct expression *expr, const char *orig_key, char **new_key)
{
	struct expression *priv;
	char buf[64];
	int diff;
	char *p;

	priv = get_netdev_priv(expr);
	if (!priv)
		return NULL;
	if (priv->type != EXPR_SYMBOL || !priv->symbol_name)
		return NULL;

	p = strstr(orig_key, "r netdev_priv($)");
	if (!p)
		return NULL;
	diff = p - orig_key;
	if (diff == 1) /* remove () */
		snprintf(buf, sizeof(buf), "$%s", orig_key + 18);
	else
		snprintf(buf, sizeof(buf), "%.*s$%s", diff, orig_key, orig_key + diff + 16);
	*new_key = alloc_sname(buf);
	return priv;
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

	// FIXME:  this function has been marked for being made static
	// Use get_name_sym_from_param_key().

	if (sym)
		*sym = NULL;

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
		char *new_key = NULL;

		expr = map_container_of_to_simpler_expr_key(arg, key, &new_key);
		if (!expr)
			return NULL;
		if (arg != expr) {
			arg = expr;
			*sym = expr_to_sym(expr);
		}
		key = new_key;
	}

	if (strstr(key, "(r netdev_priv($))")) {
		struct expression *expr;
		char *new_key = NULL;

		expr = map_netdev_priv_to_simpler_expr_key(arg, key, &new_key);
		if (!expr)
			return NULL;
		if (arg != expr) {
			arg = expr;
			*sym = expr_to_sym(expr);
		}
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

bool split_param_key(const char *value, int *param, char *key, int len)
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

	name = get_name_sym_from_param_key(expr, param, buf, &sym);
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

	if (strstr(state_name, "r netdev_priv($)"))
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

int get_return_param_key_from_var_sym(const char *name, struct symbol *sym,
				      struct expression *ret_expr,
				      const char **key)
{
	const char *param_name;
	struct symbol *ret_sym;
	char *ret_str;

	if (!ret_expr)
		return -2;

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

	return -2;
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

	param = get_return_param_key_from_var_sym(name, sym, ret_expr, key);
	if (param == -1)
		return param;

	other_name = get_param_var_sym_var_sym(name, sym, ret_expr, &other_sym);
	if (!other_name || !other_sym)
		return -2;
	param = get_param_num_from_sym(other_sym);
	if (param < 0)
		return -2;

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

int get_param_key_from_expr(struct expression *expr, struct expression *ret_expr,
			    const char **key)
{
	char *name;
	struct symbol *sym;
	int ret = -2;

	*key = NULL;
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	ret = get_param_key_from_var_sym(name, sym, ret_expr, key);
free:
	free_string(name);
	return ret;
}

const char *get_param_key_swap_dollar(struct expression *expr)
{
	struct sm_state *sm;
	const char *key, *p;
	char buf[64];
	int param;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm || slist_has_state(sm->possible, &undefined))
		return NULL;

	param = get_param_key_from_expr(expr, NULL, &key);
	if (param < 0)
		return NULL;

	p = strchr(key, '$');
	if (!p)
		return NULL;

	snprintf(buf, sizeof(buf), "%.*s%d%s", (int)(p - key + 1), key, param, p + 1);
	return alloc_sname(buf);
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

	if (!cur_func_sym)
		return GLOBAL_SCOPE;

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

struct expression *get_function_param(struct expression *expr, int param)
{
	struct expression *call;

	if (!expr) {
		sm_msg("internal: null call_expr.  param=%d", param);
		return NULL;
	}

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);

	if (call->type != EXPR_CALL)
		return NULL;

	if (param < -1)
		return NULL;

	if (param == -1) {
		if (expr->type == EXPR_ASSIGNMENT && expr->op == '=')
			return expr->left;
		return NULL;
	}

	return get_argument_from_call_expr(call->args, param);
}

char *get_name_sym_from_param_key(struct expression *expr, int param, const char *key, struct symbol **sym)
{
	struct expression *arg;
	char *name;

	if (sym)
		*sym = NULL;

	if (param == -2) // Really?  Is this sane?
		return alloc_string(key);

	arg = get_function_param(expr, param);
	if (!arg)
		return NULL;

	name = get_variable_from_key(arg, key, sym);
	if (!name || (sym && !*sym))
		goto free;

	return name;
free:
	free_string(name);
	return NULL;
}

static char *handle_container_of_assign(struct expression *expr, struct symbol **sym)
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

	snprintf(buf, sizeof(buf), "(%lld<~$%d)", sval.value, param);
	*sym = orig->symbol;
	return alloc_string(buf);
}

static char *handle_netdev_priv_assign(struct expression *expr, struct symbol **sym)
{
	struct expression *right, *arg;
	char buf[64];

	if (option_project != PROJ_KERNEL)
		return NULL;

	if (!expr || expr->type != EXPR_ASSIGNMENT)
		return NULL;
	right = strip_expr(expr->right);
	if (!right || right->type != EXPR_CALL)
		return NULL;

	if (!sym_name_is("netdev_priv", right->fn))
		return NULL;

	arg = get_argument_from_call_expr(right->args, 0);
	arg = strip_expr(arg);
	if (!arg || arg->type != EXPR_SYMBOL)
		return NULL;

	/* This isn't necessarily tied to a param */
	snprintf(buf, sizeof(buf), "(r netdev_priv($))");
	*sym = arg->symbol;
	return alloc_string(buf);
}

const char *get_container_of_str(struct expression *expr)
{
	struct smatch_state *state;

	state = get_state_expr(my_id, expr);
	if (!state)
		return NULL;
	if (!strstr(state->name, "<~$"))
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

	param_name = handle_netdev_priv_assign(expr, &param_sym);
	if (param_name && param_sym)
		goto set_state;

	goto free;

set_state:
	set_state_expr(my_id, expr->left, alloc_var_sym_state(param_name, param_sym));
free:
	free_string(param_name);
}

bool get_offset_param(const char *ret_str, int *offset, int *param)
{
	const char *p;

	if (!ret_str)
		return false;
	p = strstr(ret_str, "[(");
	if (!p)
		return false;
	p += 2;
	*offset = atoi(p);
	p = strstr(p, "<~$");
	if (!p)
		return false;
	p += 3;
	if (!isdigit(p[0]))
		return false;
	*param = atoi(p);
	return true;;
}

static void return_str_hook(struct expression *expr, const char *ret_str)
{
	struct expression *call, *arg;
	struct symbol *sym;
	int offset, param;
	char buf[32];

	if (!expr || expr->type != EXPR_ASSIGNMENT)
		return;
	call = expr;
	while (call && call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (!call || call->type != EXPR_CALL)
		return;

	if (!get_offset_param(ret_str, &offset, &param))
		return;

	arg = get_argument_from_call_expr(call->args, param);
	arg = strip_expr(arg);
	if (!arg)
		return;

	/* fixme this could be better */
	if (arg->type != EXPR_SYMBOL)
		return;
	sym = arg->symbol;

	param = get_param_num(arg);
	if (param < 0)
		return;

	snprintf(buf, sizeof(buf), "(%d<~$%d)", offset, param);
	set_state_expr(my_id, expr->left, alloc_var_sym_state(buf, sym));
}

void register_param_key(int id)
{
	my_id = id;

	set_dynamic_states(my_id);
	add_hook(&match_assign, ASSIGNMENT_HOOK_AFTER);
	add_return_string_hook(return_str_hook);
	add_modification_hook(my_id, &set_undefined);
}

