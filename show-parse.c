/*
 * sparse/show-parse.c
 *
 * Copyright (C) 2003 Transmeta Corp, all rights reserved.
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
		"const", "volatile", "[signed]", "[unsigned]",
		"[char]", "[short]", "[long]", "[long]",
		"[typdef]", "[structof]", "[unionof]", "[enum]",
		"[typeof]", "[attribute]", "inline", "<80000>",
		"[nocast]", "[noderef]", 
		NULL
	};
	ptr = names;
	while ((res = *ptr++) != NULL) {
		if (mod & 1) {
			char c;
			while ((c = *res++) != '\0')
				*p++ = c;
			*p++ = ' ';
		}
		mod >>= 1;
	}
	*p = 0;
	return buffer;
}

void show_struct_member(struct symbol *sym, void *data, int flags)
{
	if (flags & ITERATE_FIRST)
		printf(" {\n\t");
	printf("%s:%d:%ld at offset %ld", show_ident(sym->ident), sym->bit_size, sym->ctype.alignment, sym->offset);
	if (sym->fieldwidth)
		printf("[%d..%d]", sym->bit_offset, sym->bit_offset+sym->fieldwidth-1);
	if (flags & ITERATE_LAST)
		printf("\n} ");
	else
		printf(", ");
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

struct type_name {
	char *start;
	char *end;
};

static void prepend(struct type_name *name, const char *fmt, ...)
{
	static char buffer[512];
	int n;

	va_list args;
	va_start(args, fmt);
	n = vsprintf(buffer, fmt, args);
	va_end(args);

	name->start -= n;
	memcpy(name->start, buffer, n);
}

static void append(struct type_name *name, const char *fmt, ...)
{
	static char buffer[512];
	int n;

	va_list args;
	va_start(args, fmt);
	n = vsprintf(buffer, fmt, args);
	va_end(args);

	memcpy(name->end, buffer, n);
	name->end += n;
}

static void do_show_type(struct symbol *sym, struct type_name *name)
{
	int i, modlen;
	const char *mod;
	static struct ctype_name {
		struct symbol *sym;
		char *name;
	} typenames[] = {
		{ & char_ctype,  "char" },
		{ &uchar_ctype,  "unsigned char" },
		{ & short_ctype, "short" },
		{ &ushort_ctype, "unsigned short" },
		{ & int_ctype,   "int" },
		{ &uint_ctype,   "unsigned int" },
		{ & long_ctype,  "long" },
		{ &ulong_ctype,  "unsigned long" },
		{ & llong_ctype, "long long" },
		{ &ullong_ctype, "unsigned long long" },

		{ &void_ctype,   "void" },
		{ &bool_ctype,   "bool" },
		{ &string_ctype, "string" },

		{ &float_ctype,  "float" },
		{ &double_ctype, "double" },
		{ &ldouble_ctype,"long double" },
	};

	if (!sym)
		return;

	for (i = 0; i < sizeof(typenames)/sizeof(typenames[0]); i++) {
		if (typenames[i].sym == sym) {
			int len = strlen(typenames[i].name);
			*--name->start = ' ';
			name->start -= len;
			memcpy(name->start, typenames[i].name, len);
			return;
		}
	}

	/* Prepend */
	switch (sym->type) {
	case SYM_PTR:
		prepend(name, "*");
		break;
	case SYM_FN:
		prepend(name, "( ");
		break;
	case SYM_STRUCT:
		prepend(name, "struct %s ", show_ident(sym->ident));
		return;

	case SYM_UNION:
		prepend(name, "union %s ", show_ident(sym->ident));
		return;

	case SYM_ENUM:
		prepend(name, "enum %s ", show_ident(sym->ident));
		return;

	case SYM_NODE:
		append(name, "%s", show_ident(sym->ident));
		break;

	case SYM_BITFIELD:
		append(name, ":%d", sym->fieldwidth);
		break;

	case SYM_LABEL:
		append(name, "label(%s:%p)", show_ident(sym->ident), sym);
		break;

	case SYM_ARRAY:
		break;

	default:
		prepend(name, "unknown type %d", sym->type);
		break;
	}

	mod = modifier_string(sym->ctype.modifiers);
	modlen = strlen(mod);
	name->start -= modlen;    
	memcpy(name->start, mod, modlen);  

	do_show_type(sym->ctype.base_type, name);

	/* Postpend */
	if (sym->ctype.as)
		append(name, "<asn:%d>", sym->ctype.as);

	switch (sym->type) {
	case SYM_PTR:
		return; 

	case SYM_FN:
		append(name, " )( ... )");
		return;

	case SYM_ARRAY:
		append(name, "[%d]", sym->array_size);
		return;
	default:
		break;
	}
}

void show_type(struct symbol *sym)
{
	char array[200];
	struct type_name name;

	name.start = name.end = array+100;
	do_show_type(sym, &name);
	*name.end = 0;
	printf("%s", name.start);
}

void show_symbol(struct symbol *sym)
{
	struct symbol *type;

	show_type(sym);
	type = sym->ctype.base_type;
	if (!type)
		return;

	/*
	 * Show actual implementation information
	 */
	switch (type->type) {
	case SYM_STRUCT:
		symbol_iterate(type->symbol_list, show_struct_member, NULL);
		break;

	case SYM_UNION:
		symbol_iterate(type->symbol_list, show_struct_member, NULL);
		break;

	case SYM_FN:
		printf("\n");		
		show_statement(type->stmt);
		break;

	default:
		break;
	}

	if (sym->initializer) {
		printf(" = ");
		show_expression(sym->initializer);
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
	case STMT_NONE:
		printf("\tNONE");
		break;
	
	case STMT_CONTINUE:
		printf("\tcontinue");
		break;

	case STMT_LABEL:
		show_symbol(stmt->label_identifier);
		printf(":\n");
		show_statement(stmt->label_statement);
		break;

	case STMT_GOTO:
		if (stmt->goto_expression) {
			printf("\tgoto *");
			show_expression(stmt->goto_expression);
		} else {
			printf("\tgoto ");
			show_symbol(stmt->goto_label);
		}
		break;
	case STMT_ASM:
		printf("\tasm( .... )");
		break;
		
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

static void show_size(struct symbol *sym)
{
	if (sym)
		printf("%d:%ld", sym->bit_size, sym->ctype.alignment);
}

static void show_one_expression(struct expression *expr, void *sep, int flags)
{
	show_expression(expr);
	if (!(flags & ITERATE_LAST))
		printf("%s", (const char *)sep);
}

void show_expression_list(struct expression_list *list, const char *sep)
{
	expression_iterate(list, show_one_expression, (void *)sep);
}

/*
 * Print out an expression
 */
void show_expression(struct expression *expr)
{
	if (!expr)
		return;

	printf("< (");
	show_size(expr->ctype);
	show_type(expr->ctype);
	printf(") ");
	switch (expr->type) {
	case EXPR_CALL:
		show_expression(expr->fn);
		printf("( ");
		show_expression_list(expr->args, ", ");
		printf(" )");
		break;
		
	case EXPR_ASSIGNMENT:
		show_expression(expr->left);
		printf(" %s ", show_special(expr->op));
		show_expression(expr->right);
		break;
	case EXPR_BINOP:
	case EXPR_COMMA:
	case EXPR_COMPARE:
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
	case EXPR_SYMBOL:
		if (!expr->symbol) {
			warn(expr->pos, "undefined symbol '%s'", show_ident(expr->symbol_name));
			printf("<nosymbol>");
			break;
		}
		printf("<%s:", show_ident(expr->symbol_name));
		show_type(expr->symbol->ctype.base_type);
		printf(">");
		break;
	case EXPR_DEREF:
		show_expression(expr->deref);
		printf("%s", show_special(expr->op));
		printf("%s", show_ident(expr->member));
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
	case EXPR_STRING:
		printf("%s", show_string(expr->string));
		break;
	case EXPR_SIZEOF:
		printf("sizeof(");
		if (expr->cast_type)
			show_type(expr->cast_type);
		else
			show_expression(expr->cast_expression);
		printf(")");
		break;
	case EXPR_BITFIELD:
		printf("bits[%d-%d](", expr->bitpos, expr->bitpos + expr->nrbits - 1);
		show_expression(expr->address);
		printf(")");
		break;
	case EXPR_INITIALIZER:
		printf(" {\n\t");
		show_expression_list(expr->expr_list, ", ");
		printf("\n} ");
		break;
	case EXPR_IDENTIFIER:
		printf("%s: ", show_ident(expr->expr_ident));
		break;
	case EXPR_INDEX:
		if (expr->idx_from == expr->idx_to) {
			printf("[%d]: ", expr->idx_from);
		} else {
			printf("[%d ... %d]: ", expr->idx_from, expr->idx_to);
		}
		break;
	case EXPR_CONDITIONAL:
		show_expression(expr->conditional);
		printf(" ? ");
		show_expression(expr->cond_true);
		printf(" : ");
		show_expression(expr->cond_false);
		break;
	case EXPR_STATEMENT:
		printf("({\n\t");
		show_statement(expr->statement);
		printf("\n})");
		break;
	}
	printf(" >");
}


