/*
 * Copyright (C) 2017 Oracle.  All rights reserved.
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
 * One problem that I have is that it's really hard to track how pointers are
 * passed around.  For example, it would be nice to know that the probe() and
 * remove() functions get the same pci_dev pointer.  It would be good to know
 * what pointers we're passing to the open() and close() functions.  But that
 * information gets lost in a call tree full of function pointer calls.
 *
 * I think the first step is to start naming specific pointers.  So when a
 * pointer is allocated, then it gets a tag.  So calls to kmalloc() generate a
 * tag.  But we might not use that, because there might be a better name like
 * framebuffer_alloc(). The framebuffer_alloc() is interesting because there is
 * one per driver and it's passed around to all the file operations.
 *
 * Perhaps we could make a list of functions like framebuffer_alloc() which take
 * a size and say that those are the interesting alloc functions.
 *
 * Another place where we would maybe name the pointer is when they are passed
 * to the probe().  Because that's an important pointer, since there is one
 * per driver (sort of).
 *
 * My vision is that you could take a pointer and trace it back to a global.  So
 * I'm going to track that pointer_tag - 28 bytes takes you to another pointer
 * tag.  You could follow that one back and so on.  Also when we pass a pointer
 * to a function that would be recorded as sort of a link or path or something.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

#include <openssl/md5.h>

static int my_id;

static struct smatch_state *alloc_tag_state(mtag_t tag)
{
	struct smatch_state *state;
	char buf[64];

	state = __alloc_smatch_state(0);
	snprintf(buf, sizeof(buf), "%lld", tag);
	state->name = alloc_sname(buf);
	state->data = malloc(sizeof(mtag_t));
	*(mtag_t *)state->data = tag;

	return state;
}

static mtag_t str_to_tag(const char *str)
{
	unsigned char c[MD5_DIGEST_LENGTH];
	unsigned long long *tag = (unsigned long long *)&c;
	MD5_CTX mdContext;
	int len;

	len = strlen(str);
	MD5_Init(&mdContext);
	MD5_Update(&mdContext, str, len);
	MD5_Final(c, &mdContext);

	*tag &= ~(1ULL << 63);

	return *tag;
}

static void alloc_assign(const char *fn, struct expression *expr, void *unused)
{
	struct expression *left, *right;
	char *left_name, *right_name;
	struct symbol *left_sym;
	char buf[256];
	mtag_t tag;

	if (expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return;
	left = strip_expr(expr->left);
	right = strip_expr(expr->right);
	if (right->type != EXPR_CALL || right->fn->type != EXPR_SYMBOL)
		return;

	left_name = expr_to_str_sym(left, &left_sym);
	right_name = expr_to_str(right);

	snprintf(buf, sizeof(buf), "%s %s %s %s", get_filename(), get_function(),
		 left_name, right_name);
	tag = str_to_tag(buf);

	sql_insert_mtag_about(tag, left_name, right_name);

	if (left_name && left_sym)
		set_state(my_id, left_name, left_sym, alloc_tag_state(tag));

	free_string(left_name);
	free_string(right_name);
}

int get_toplevel_mtag(struct symbol *sym, mtag_t *tag)
{
	char buf[256];

	if (!sym)
		return 0;

	if (!sym->ident ||
	    !(sym->ctype.modifiers & MOD_TOPLEVEL))
		return 0;

	snprintf(buf, sizeof(buf), "%s %s",
		 (sym->ctype.modifiers & MOD_STATIC) ? get_filename() : "extern",
		 sym->ident->name);
	*tag = str_to_tag(buf);
	return 1;
}

int get_deref_mtag(struct expression *expr, mtag_t *tag)
{
	mtag_t container_tag, member_tag;
	int offset;

	/*
	 * I'm not totally sure what I'm doing...
	 *
	 * This is supposed to get something like "global_var->ptr", but I don't
	 * feel like it's complete at all.
	 *
	 */

	if (!get_mtag(expr->unop, &container_tag))
		return 0;

	offset = get_member_offset_from_deref(expr);
	if (offset < 0)
		return 0;

	if (!mtag_map_select_tag(container_tag, -offset, &member_tag))
		return 0;

	*tag = member_tag;
	return 1;
}

static void global_variable(struct symbol *sym)
{
	mtag_t tag;

	if (!get_toplevel_mtag(sym, &tag))
		return;

	sql_insert_mtag_about(tag,
			      sym->ident->name,
			      (sym->ctype.modifiers & MOD_STATIC) ? get_filename() : "extern");
}

static void db_returns_buf_size(struct expression *expr, int param, char *unused, char *math)
{
	struct expression *call;
	struct range_list *rl;

	if (expr->type != EXPR_ASSIGNMENT)
		return;
	call = strip_expr(expr->right);

	if (!parse_call_math_rl(call, math, &rl))
		return;
//	rl = cast_rl(&int_ctype, rl);
//	set_state_expr(my_size_id, expr->left, alloc_estate_rl(rl));
}

static void match_call_info(struct expression *expr)
{
	struct smatch_state *state;
	struct expression *arg;
	int i = -1;

	FOR_EACH_PTR(expr->args, arg) {
		i++;
		state = get_state_expr(my_id, arg);
		if (!state || !state->data)
			continue;
		sql_insert_caller_info(expr, MEMORY_TAG, i, "$", state->name);
	} END_FOR_EACH_PTR(arg);
}

static void save_caller_info(const char *name, struct symbol *sym, char *key, char *value)
{
	struct smatch_state *state;
	char fullname[256];
	mtag_t tag;

	if (strncmp(key, "$", 1) != 0)
		return;

	tag = atoll(value);
	snprintf(fullname, 256, "%s%s", name, key + 1);
	state = alloc_tag_state(tag);
	set_state(my_id, fullname, sym, state);
}

static int get_array_mtag(struct expression *expr, mtag_t *tag)
{
	struct expression *base, *offset;
	sval_t sval;
	mtag_t base_tag;
	char buf[256];

	if (!is_array(expr))
		return 0;

	base = get_array_base(expr);
	if (!base)
		return 0;
	offset = get_array_offset(expr);
	if (!offset)
		return 0;

	if (!get_implied_value(offset, &sval))
		return 0;

	if (!get_mtag(base, &base_tag))
		return 0;

	snprintf(buf, sizeof(buf), "%lld + %lld", base_tag, sval.value);
	*tag = str_to_tag(buf);

	return 1;
}

static int get_mtag_cnt;
int get_mtag(struct expression *expr, mtag_t *tag)
{
	struct smatch_state *state;
	int ret = 0;

	expr = strip_expr(expr);
	if (!expr)
		return 0;

	if (get_mtag_cnt > 0)
		return 0;

	get_mtag_cnt++;

	/* FIXME:  This doesn't feel like the right thing at all */
	if (expr->type == EXPR_PREOP) {
		if (expr->op == '&')
			expr = strip_expr(expr->unop);
		if (expr->op == '*')
			expr = strip_expr(expr->unop);
	}

	switch (expr->type) {
	case EXPR_SYMBOL:
		if (get_toplevel_mtag(expr->symbol, tag)) {
			ret = 1;
			goto dec_cnt;
		}
		break;
	case EXPR_DEREF:
		if (get_deref_mtag(expr, tag)) {
			ret = 1;
			goto dec_cnt;
		}
		break;
	case EXPR_BINOP:
		ret = get_array_mtag(expr, tag);
		goto dec_cnt;
	}

	state = get_state_expr(my_id, expr);
	if (!state)
		goto dec_cnt;
	if (state->data) {
		*tag = *(mtag_t *)state->data;
		ret = 1;
		goto dec_cnt;
	}

dec_cnt:
	get_mtag_cnt--;
	return ret;
}

int create_mtag_alias(mtag_t tag, struct expression *expr, mtag_t *new)
{
	char buf[256];
	int lines_from_start;
	char *str;

	/*
	 * We need the alias to be unique.  It's not totally required that it
	 * be the same from one DB build to then next, but it makes debugging
	 * a bit simpler.
	 *
	 */

	if (!cur_func_sym)
		return 0;

	lines_from_start = expr->pos.line - cur_func_sym->pos.line;
	str = expr_to_str(expr);
	snprintf(buf, sizeof(buf), "%lld %d %s", tag, lines_from_start, str);
	free_string(str);

	*new = str_to_tag(buf);

	return 1;
}

int expr_to_mtag_name_offset(struct expression *expr, mtag_t *tag, char **name, int *offset)
{
	static char buf[256];

	*offset = 0;
	*name = 0;

	expr = strip_expr(expr);
	if (!expr)
		return 0;

	if (expr->type ==  EXPR_DEREF) {
		*offset = get_member_offset_from_deref(expr);
		if (*offset < 0)
			return 0;
		snprintf(buf, sizeof(buf), "$->%s", expr->member->name);
		*name = buf;
		return get_mtag(expr->deref, tag);
	}

	snprintf(buf, sizeof(buf), "*$");
	*name = buf;

	return get_mtag(expr, tag);
}

void register_mtag(int id)
{
	my_id = id;

	add_hook(&global_variable, BASE_HOOK);

	add_function_assign_hook("kmalloc", &alloc_assign, NULL);
	add_function_assign_hook("kzalloc", &alloc_assign, NULL);

	select_return_states_hook(BUF_SIZE, &db_returns_buf_size);

	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
	select_caller_info_hook(save_caller_info, MEMORY_TAG);
}
