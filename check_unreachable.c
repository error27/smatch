/*
 * Copyright (C) 2014 Oracle.
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

static int my_id;

static int empty_statement(struct statement *stmt)
{
	if (!stmt)
		return 0;
	if (stmt->type == STMT_EXPRESSION && !stmt->expression)
		return 1;
	return 0;
}

static int is_last_stmt(struct statement *cur_stmt)
{
	struct symbol *fn = get_base_type(cur_func_sym);
	struct statement *stmt;

	if (!fn)
		return 0;
	stmt = fn->stmt;
	if (!stmt)
		stmt = fn->inline_stmt;
	if (!stmt || stmt->type != STMT_COMPOUND)
		return 0;
	stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (stmt == cur_stmt)
		return 1;
	return 0;
}

static void print_unreached_initializers(struct symbol_list *sym_list)
{
	struct symbol *sym;

	FOR_EACH_PTR(sym_list, sym) {
		if (sym->initializer)
			sm_msg("info: '%s' is not actually initialized (unreached code).",
				(sym->ident ? sym->ident->name : "this variable"));
	} END_FOR_EACH_PTR(sym);
}

static void print_unreached(struct statement *stmt)
{
	static int print = 1;

	if (__inline_fn)
		return;

	if (!__path_is_null()) {
		print = 1;
		return;
	}
	if (!print)
		return;

	switch (stmt->type) {
	case STMT_COMPOUND: /* after a switch before a case stmt */
	case STMT_RANGE:
	case STMT_CASE:
	case STMT_LABEL:
		return;
	case STMT_DECLARATION: /* switch (x) { int a; case foo: ... */
		print_unreached_initializers(stmt->declaration);
		return;
	case STMT_RETURN: /* gcc complains if you don't have a return statement */
		if (is_last_stmt(stmt))
			return;
		break;
	case STMT_GOTO:
		/* people put extra breaks inside switch statements */
		if (stmt->goto_label && stmt->goto_label->type == SYM_NODE &&
		    strcmp(stmt->goto_label->ident->name, "break") == 0)
			return;
		break;
	default:
		break;
	}
	if (empty_statement(stmt))
		return;
	if (!option_spammy)
		return;
	sm_msg("info: ignoring unreachable code.");
	print = 0;
}

void check_unreachable(int id)
{
	my_id = id;

	add_hook(&print_unreached, STMT_HOOK);
}
