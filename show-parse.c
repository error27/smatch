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
		"[typeof]", "[attribute]", "inline", "[addressable]",
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
		return;
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
int show_statement(struct statement *stmt)
{
	if (!stmt)
		return 0;
	switch (stmt->type) {
	case STMT_RETURN:
		printf("\treturn ");
		show_expression(stmt->expression);
		return 0;
	case STMT_COMPOUND: {
		struct statement *s;
		int last;

		if (stmt->syms) {
			printf("\t");
			show_symbol_list(stmt->syms, "\n\t");
			printf("\n\n");
		}
		FOR_EACH_PTR(stmt->stmts, s) {
			last = show_statement(s);
		} END_FOR_EACH_PTR;
		return last;
	}

	case STMT_EXPRESSION:
		return show_expression(stmt->expression);
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
	return 0;
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

static int new_pseudo(void)
{
	static int nr = 0;
	return ++nr;
}

static int show_call_expression(struct expression *expr)
{
	int pseudoarg[45];
	struct expression *arg, *fn;
	int fncall, retval;
	int count = 0, i;

	FOR_EACH_PTR(expr->args, arg) {
		int new = show_expression(arg);
		pseudoarg[count++] = new;
	} END_FOR_EACH_PTR;

	fn = expr->fn;
	/* Remove dereference, if any */
	if (fn->type == EXPR_PREOP)
		fn = fn->unop;
	fncall = show_expression(fn);
	retval = new_pseudo();
	printf("v%d = call *v%d(", retval, fncall);
	for (i = 0; i < count; i++)
		printf("v%d%s", pseudoarg[i], (i == count-1) ? "" : ", ");
	printf(")\n");
	return retval;
}

static int show_assignment(struct expression *expr)
{
	int src = show_expression(expr->right);
	struct expression *target = expr->left;
	int dst;

	/* Is it a regular dereferece? */
	if (target->type == EXPR_PREOP) {
		dst = show_expression(target->unop);
		printf("store(v%d,v%d)\n", src, dst);
		return src;
	}

	/* Bitfield.. */
	dst = show_expression(target->address);
	printf("bit_insert(v%d,v%d[%d-%d]\n", src, dst,
		target->bitpos, target->bitpos + target->nrbits);
	return src;
}

static int show_binop(struct expression *expr)
{
	int left = show_expression(expr->left);
	int right = show_expression(expr->right);
	int new = new_pseudo();

	printf("v%d = v%d %s v%d\n", new, left, show_special(expr->op), right);
	return new;
}

static int show_preop(struct expression *expr)
{
	int op = show_expression(expr->unop);
	int new = new_pseudo();

	printf("v%d = %sv%d\n", new, show_special(expr->op), op);
	return new;
}	

static int show_postop(struct expression *expr)
{
	int op = show_expression(expr->unop);
	int new = new_pseudo();

	printf("v%d = v%d%s\n", new, op, show_special(expr->op));
	return new;
}	

static int show_symbol_expr(struct expression *expr)
{
	int new = new_pseudo();
	printf("v%d = address of '%s'\n", new, show_ident(expr->symbol_name));
	return new;
}

static int show_cast_expr(struct expression *expr)
{
	int op = show_expression(expr->cast_expression);
	int oldbits, newbits;
	int new;

	oldbits = expr->cast_expression->ctype->bit_size;
	newbits = expr->cast_type->bit_size;
	if (oldbits == newbits)
		return op;
	new = new_pseudo();
	printf("v%d = <cast from %d to %d bits> v%d\n",
		new, oldbits, newbits, op);
	return new;
}

static int type_is_signed(struct symbol *sym)
{
	while (sym->type != SYM_BASETYPE)
		sym = sym->ctype.base_type;
	return !(sym->ctype.modifiers & MOD_UNSIGNED);
}

static int show_value(struct expression *expr)
{
	int new = new_pseudo();
	struct symbol *ctype = expr->ctype;
	unsigned long long value = expr->value;
	unsigned long long mask = 1ULL << (ctype->bit_size-1);

	if (value & mask) {
		if (type_is_signed(ctype)) {
			long long svalue = value | ~(mask-1);
			printf("v%d = %lld\n", new, svalue);
			return new;
		}
	}
	printf("v%d = %llu\n", new, value);
	return new;
}

static int show_string_expr(struct expression *expr)
{
	int new = new_pseudo();

	printf("v%d = %s\n", new, show_string(expr->string));
	return new;
}

static int show_bitfield_expr(struct expression *expr)
{
	int address = show_expression(expr->address);
	int new = new_pseudo();

	printf("v%d = (extract bits[%d-%d]) v%d\n",
		new, expr->bitpos, expr->bitpos + expr->nrbits - 1, address);
	return new;
}

static int show_conditional_expr(struct expression *expr)
{
	int cond = show_expression(expr->conditional);
	int true = show_expression(expr->cond_true);
	int false = show_expression(expr->cond_false);
	int new = new_pseudo();

	if (!true)
		true = cond;
	printf("v%d = v%d ? v%d : v%d\n", new, cond, true, false);
	return new;
}

static int show_statement_expr(struct expression *expr)
{
	return show_statement(expr->statement);
}

/*
 * Print out an expression. Return the pseudo that contains the
 * variable.
 */
int show_expression(struct expression *expr)
{
	if (!expr)
		return 0;

	switch (expr->type) {
	case EXPR_CALL:
		return show_call_expression(expr);
		
	case EXPR_ASSIGNMENT:
		return show_assignment(expr);

	case EXPR_BINOP:
	case EXPR_COMMA:
	case EXPR_COMPARE:
	case EXPR_LOGICAL:
		return show_binop(expr);
	case EXPR_PREOP:
		return show_preop(expr);
	case EXPR_POSTOP:
		return show_postop(expr);
	case EXPR_SYMBOL:
		return show_symbol_expr(expr);
	case EXPR_DEREF:
	case EXPR_SIZEOF:
		warn(expr->pos, "invalid expression after evaluation");
		return 0;
	case EXPR_CAST:
		return show_cast_expr(expr);
	case EXPR_VALUE:
		return show_value(expr);
	case EXPR_STRING:
		return show_string_expr(expr);
	case EXPR_BITFIELD:
		return show_bitfield_expr(expr);
	case EXPR_INITIALIZER:
		warn(expr->pos, "unable to show initializer expression");
		return 0;
	case EXPR_IDENTIFIER:
		warn(expr->pos, "unable to show identifier expression");
		return 0;
	case EXPR_INDEX:
		warn(expr->pos, "unable to show index expression");
		return 0;
	case EXPR_CONDITIONAL:
		return show_conditional_expr(expr);
	case EXPR_STATEMENT:
		return show_statement_expr(expr);
	}
	return 0;
}


