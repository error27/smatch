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
		"typdef", "structof", "unionof", "enum",
		"typeof", "attribute",
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

void show_struct_member(struct symbol *sym, void *data, int flags)
{
	if (flags & ITERATE_FIRST)
		printf(" { ");
	printf("%s:%d:%d at offset %ld", show_token(sym->token), sym->bit_size, sym->alignment, sym->offset);
	if (flags & ITERATE_LAST)
		printf(" } ");
	else
		printf(", ");
}

void show_type_details(unsigned int modifiers, struct symbol *sym)
{
	if (!sym) {
		printf(" <notype>");
		return;
	}

	if (sym == &int_type) {
		if (modifiers & (MOD_CHAR | MOD_SHORT | MOD_LONG))
			return;
		printf(" int");
		return;
	}
	if (sym == &fp_type) {
		printf(" float");
		return;
	}
	if (sym == &void_type) {
		printf(" void");
		return;
	}
	if (sym == &vector_type) {
		printf(" vector");
		return;
	}
	if (sym == &bad_type) {
		printf(" <badtype>");
		return;
	}
	if (sym->type == SYM_STRUCT) {
		printf(" struct %s", show_token(sym->token));
		symbol_iterate(sym->symbol_list, show_struct_member, NULL);
		return;
	}
	if (sym->type == SYM_UNION) {
		printf(" union %s", show_token(sym->token)); 
		symbol_iterate(sym->symbol_list, show_struct_member, NULL);
		return;
	}
	if (sym->type == SYM_ENUM) {
		printf(" enum %s", show_token(sym->token));
		return;
	}

	if (sym->type == SYM_PTR)
		printf(" *");
	else if (sym->type == SYM_FN)
		printf(" <fn>");
	else
		printf(" strange type %d", sym->type);

	if (sym->token)
		printf(" '%s'", show_token(sym->token));

	printf("%s", modifier_string(sym->ctype.modifiers));
	show_type_details(sym->ctype.modifiers, sym->ctype.base_type);
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

	printf("%d:%d %s", sym->bit_size, sym->alignment, modifier_string(sym->ctype.modifiers));

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
		show_type_details(ctype->modifiers, ctype->base_type);
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
	if (!stmt)
		return;
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
		
	case STMT_ITERATOR: {
		struct statement  *pre_statement = stmt->iterator_pre_statement;
		struct expression *pre_condition = stmt->iterator_pre_condition;
		struct statement  *statement = stmt->iterator_statement;
		struct statement  *post_statement = stmt->iterator_post_statement;
		struct expression *post_condition = stmt->iterator_post_condition;

		/*
		 * THIS IS ONLY APPROXIMATE!
		 *
		 * Real iterators are more generic than
		 * any of for/while/do-while, and can't
		 * be printed out as C without goto's
		 */
		if (post_statement || !post_condition) {
			printf("\tfor ( ");
			show_statement(pre_statement);
			printf(" ; ");
			show_expression(pre_condition);
			printf(" ; ");
			show_statement(post_statement);
			printf(" )\n");
			show_statement(statement);
		} else if (pre_condition) {
			if (pre_statement) {
				show_statement(pre_statement);
				printf(";\n");
			}
			printf("\twhile (");
			show_expression(pre_condition);
			printf(")\n");
			show_statement(statement);
		} else {
			if (pre_statement) {
				show_statement(pre_statement);
				printf(";\n");
			}
			printf("\tdo\n");
			show_statement(statement);
			printf("\twhile (");
			show_expression(post_condition);
			printf(")");
		}
		break;
	}
		
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
	case EXPR_VALUE:
		printf("(%lld)", expr->value);
		break;
	default:
		printf("WTF");
	}
	printf(" >");
}


