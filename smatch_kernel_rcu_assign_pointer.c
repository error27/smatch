/*
 * Copyright 2025 Linaro Ltd.
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

#include "smatch.h"
#include "smatch_extra.h"

static int my_id;

struct expression *_r_a_p__v;

static void match_declaration(struct symbol *sym)
{
	if (!sym->initializer || !sym->ident)
		return;
	if (strcmp(sym->ident->name, "_r_a_p__v") != 0)
		return;
	_r_a_p__v = strip_parens(sym->initializer);
}

static struct expression *get_p_initializer(struct expression *expr)
{
	if (!expr || expr->type != EXPR_PREOP || expr->op != '*')
		return NULL;
	expr = expr->unop;
	if (!expr || expr->type != EXPR_SYMBOL || !expr->symbol_name)
		return NULL;
	if (strcmp(expr->symbol_name->name, "__p") != 0)
		return NULL;
	return strip_parens(expr->symbol->initializer);
}

static bool is__u_dot__c(struct expression *expr)
{
	if (!expr || expr->type != EXPR_PREOP || expr->op != '*')
		return false;
	expr = strip_expr(expr->unop);
	if (!expr || expr->type != EXPR_DEREF || !expr->member)
		return false;
	if (strcmp(expr->member->name, "__c") != 0)
		return false;
	return true;
}

static void match_asm(struct statement *stmt)
{
	struct asm_operand *in, *out;
	struct expression *p, *assign;

	if (!stmt || stmt->type != STMT_ASM)
		return;
	if (!stmt->asm_outputs || !stmt->asm_inputs)
		return;
	if (!_r_a_p__v ||
	    _r_a_p__v->pos.line != stmt->pos.line ||
	    _r_a_p__v->pos.pos != stmt->pos.pos)
		return;

	out = first_ptr_list((struct ptr_list *)stmt->asm_outputs);
	in = first_ptr_list((struct ptr_list *)stmt->asm_inputs);

	p = get_p_initializer(out->expr);
	if (!p)
		return;
	if (!is__u_dot__c(in->expr))
		return;

	p = deref_expression(p);
	assign = assign_expression(p, '=', _r_a_p__v);
	__split_expr(assign);
}

void register_kernel_rcu_assign_pointer(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_hook(match_declaration, DECLARATION_HOOK);
	add_hook(&match_asm, ASM_HOOK);
}

