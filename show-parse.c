/*
 * sparse/show-parse.c
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
 *
 * Print out results of parsing for debugging and testing.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "scope.h"
#include "expression.h"

/*
 * Symbol type printout. The type system is by far the most
 * complicated part of C - everything else is trivial.
 */
const char *modifier_string(unsigned long mod)
{
	static char buffer[100];
	char *p = buffer;
	const char *res,**ptr, *names[] = {
		"auto", "register", "static", "extern",
		"const", "volatile", "signed", "unsigned",
		"char", "short", "long", "long",
		NULL
	};
	ptr = names;
	while ((res = *ptr++) != NULL) {
		if (mod & 1) {
			char c;
			*p++ = ' ';
			while ((c = *res++) != '\0')
				*p++ = c;
		}
		mod >>= 1;
	}
	*p++ = 0;
	*p++ = 0;
	return buffer+1;
}

const char *type_string(unsigned int modifiers, struct symbol *sym)
{
	if (!sym)
		return "<notype>";
		
	if (sym == &int_type) {
		if (modifiers & (SYM_CHAR | SYM_SHORT | SYM_LONG))
			return "";
		return "int";
	}
	if (sym == &fp_type)
		return "float";
	if (sym == &void_type)
		return "void";
	if (sym == &vector_type)
		return "vector";
	if (sym == &bad_type)
		return "bad type";
	if (sym->token)
		return show_token(sym->token);
	return "typedef";
}

static void show_one_symbol(struct symbol *sym, void *sep, int flags)
{
	show_symbol(sym);
	if (!(flags & ITERATE_LAST))
		printf("%s", (const char *)sep);
}

void show_symbol_list(struct symbol_list *list, const char *sep)
{
	symbol_iterate(list, show_one_symbol, (void *)sep);
}

void show_type_list(struct symbol *sym)
{
	while (sym) {
		show_symbol(sym);
		sym = sym->next;
	}
}

void show_type(struct symbol *sym)
{
	struct ctype *ctype = &sym->ctype;

	if (!sym) {
		printf(" <typeless>");
		return;
	}

	printf("%s", modifier_string(sym->ctype.modifiers));

	switch (sym->type) {
	case SYM_PTR:
		show_type(sym->ctype.base_type);
		printf(" *");
		break;

	case SYM_FN:
		printf(" ");
		show_type(sym->ctype.base_type);
		printf(" <fn>(");
		show_symbol_list(sym->arguments, ", ");
		printf(")");
		break;

	case SYM_ARRAY:
		printf("<array of>");
		show_type(sym->ctype.base_type);
		printf("[ ... ]");
		break;

	default:
		printf("%s", type_string(ctype->modifiers, ctype->base_type));
		break;
	}
}

void show_symbol(struct symbol *sym)
{
	if (!sym) {
		printf("<anon symbol>");
		return;
	}
	switch (sym->type) {
	case SYM_FN:
		printf("%s: ", show_token(sym->token));
		show_type(sym);
		printf("\n");
		show_statement(sym->stmt);
		break;
	default:
		printf("");
		show_type(sym);
		printf(": %s", show_token(sym->token));
		break;
	}
}


/*
 * Print out a statement
 */
void show_statement(struct statement *stmt)
{
	if (!stmt) {
		printf("\t<nostatement>");
		return;
	}
	switch (stmt->type) {
	case STMT_RETURN:
		printf("\treturn ");
		show_expression(stmt->expression);
		break;
	case STMT_COMPOUND:
		printf("{\n");
		if (stmt->syms) {
			printf("\t");
			show_symbol_list(stmt->syms, "\n\t");
			printf("\n\n");
		}
		show_statement_list(stmt->stmts, ";\n");
		printf("\n}\n\n");
		break;
	case STMT_EXPRESSION:
		printf("\t");
		show_expression(stmt->expression);
		return;
	case STMT_IF:
		printf("\tif (");
		show_expression(stmt->if_conditional);
		printf(")\n");
		show_statement(stmt->if_true);
		if (stmt->if_false) {
			printf("\nelse\n");
			show_statement(stmt->if_false);
		}
		break;
	case STMT_SWITCH:
		printf("\tswitch (");
		show_expression(stmt->switch_expression);
		printf(")\n");
		show_statement(stmt->switch_statement);
		break;

	case STMT_CASE:
		if (!stmt->case_expression)
			printf("default");
		else {
			printf("case ");
			show_expression(stmt->case_expression);
			if (stmt->case_to) {
				printf(" ... ");
				show_expression(stmt->case_to);
			}
		}
		printf(":");
		show_statement(stmt->case_statement);
		break;

	case STMT_BREAK:
		printf("\tbreak");
		break;
		
	case STMT_WHILE:
		printf("\twhile (");
		show_expression(stmt->e1);
		printf(")\n");
		show_statement(stmt->iterate);
		break;
		
	case STMT_DO:
		printf("\tdo");
		show_statement(stmt->iterate);
		printf("\nwhile (");
		show_expression(stmt->e1);
		printf(")\n");
		break;
		
	case STMT_FOR:
		printf("\tfor (" );
		show_expression(stmt->e1);
		printf(" ; ");
		show_expression(stmt->e2);
		printf(" ; ");
		show_expression(stmt->e3);
		printf(")\n");
		show_statement(stmt->iterate);
		break;
		
	default:
		printf("WTF");
	}
}

static void show_one_statement(struct statement *stmt, void *sep, int flags)
{
	show_statement(stmt);
	if (!(flags & ITERATE_LAST))
		printf("%s", (const char *)sep);
}

void show_statement_list(struct statement_list *stmt, const char *sep)
{
	statement_iterate(stmt, show_one_statement, (void *)sep);
}

/*
 * Print out an expression
 */
void show_expression(struct expression *expr)
{
	if (!expr)
		return;

	printf("< ");
	switch (expr->type) {
	case EXPR_BINOP:
		show_expression(expr->left);
		printf(" %s ", show_special(expr->op));
		show_expression(expr->right);
		break;
	case EXPR_PREOP:
		printf("%s<", show_special(expr->op));
		show_expression(expr->unop);
		printf(">");
		break;
	case EXPR_POSTOP:
		show_expression(expr->unop);
		printf(" %s ", show_special(expr->op));
		break;
	case EXPR_CONSTANT:
		printf("%s", show_token(expr->token));
		break;
	case EXPR_SYMBOL:
		if (!expr->symbol) {
			warn(expr->token, "undefined symbol '%s'", show_token(expr->token));
			printf("<nosymbol>");
			break;
		}
		printf("<%s:", show_token(expr->symbol->token));
		show_type(expr->symbol);
		printf(">");
		break;
	case EXPR_DEREF:
		show_expression(expr->deref);
		printf("%s", show_special(expr->op));
		printf("%s", show_token(expr->member));
		break;
	case EXPR_CAST:
		printf("<cast>(");
		show_type(expr->cast_type);
		printf(")");
		show_expression(expr->cast_expression);
		break;
	default:
		printf("WTF");
	}
	printf(" >");
}


