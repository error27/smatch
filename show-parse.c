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
#include "target.h"

static void do_debug_symbol(struct symbol *sym, int indent)
{
	static const char indent_string[] = "                                  ";
	static const char *typestr[] = {
		"base", "node", "ptr.", "fn..",
		"arry", "strt", "unin", "enum",
		"tdef", "tpof", "memb", "bitf",
		"labl"
	};

	if (!sym)
		return;
	fprintf(stderr, "%.*s%s%3d:%lu %lx %s (as: %d, context: %x:%x)\n",
		indent, indent_string, typestr[sym->type],
		sym->bit_size, sym->ctype.alignment,
		sym->ctype.modifiers, show_ident(sym->ident),
		sym->ctype.as, sym->ctype.context, sym->ctype.contextmask);
	do_debug_symbol(sym->ctype.base_type, indent+2);
}

void debug_symbol(struct symbol *sym)
{
	do_debug_symbol(sym, 0);
}

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

const char *show_typename(struct symbol *sym)
{
	static char array[200];
	struct type_name name;

	name.start = name.end = array+100;
	do_show_type(sym, &name);
	*name.end = 0;
	return name.start;
}

void show_symbol(struct symbol *sym)
{
	struct symbol *type;

	if (!sym)
		return;
		
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

static int show_return_stmt(struct statement *stmt)
{
	struct expression *expr = stmt->expression;

	if (expr) {
		int val = show_expression(expr);
		printf("\tmov.%d\t\tretval,v%d\n",
			expr->ctype->bit_size, val);
	}
	printf("\tret\n");
	return 0;
}

static int show_symbol_init(struct symbol *sym);

/*
 * Print out a statement
 */
int show_statement(struct statement *stmt)
{
	if (!stmt)
		return 0;
	switch (stmt->type) {
	case STMT_RETURN:
		return show_return_stmt(stmt);
	case STMT_COMPOUND: {
		struct symbol *sym;
		struct statement *s;
		int last;

		FOR_EACH_PTR(stmt->syms, sym) {
			show_symbol_init(sym);
		} END_FOR_EACH_PTR;

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
	struct symbol *direct;
	struct expression *arg, *fn;
	int fncall, retval;
	int framesize;

	if (!expr->ctype) {
		warn(expr->pos, "\tcall with no type!");
		return 0;
	}

	framesize = 0;
	FOR_EACH_PTR_REVERSE(expr->args, arg) {
		int new = show_expression(arg);
		int size = arg->ctype->bit_size;
		printf("\tpush.%d\t\tv%d\n", size, new);
		framesize += size >> 3;
	} END_FOR_EACH_PTR;

	fn = expr->fn;

	/* Remove dereference, if any */
	direct = NULL;
	if (fn->type == EXPR_PREOP) {
		if (fn->unop->type == EXPR_SYMBOL) {
			struct symbol *sym = fn->unop->symbol;
			if (sym->ctype.base_type->type == SYM_FN)
				direct = sym;
		}
	}
	if (direct) {
		printf("\tcall\t\t%s\n", show_ident(direct->ident));
	} else {
		fncall = show_expression(fn);
		printf("\tcall\t\t*v%d\n", fncall);
	}
	if (framesize)
		printf("\tadd.%d\t\tvSP,vSP,$%d\n", BITS_IN_POINTER, framesize);

	retval = new_pseudo();
	printf("\tmov.%d\t\tv%d,retval\n", expr->ctype->bit_size, retval);
	return retval;
}

static int show_binop(struct expression *expr)
{
	int left = show_expression(expr->left);
	int right = show_expression(expr->right);
	int new = new_pseudo();
	const char *opname;
	static const char *name[] = {
		['+'] = "add", ['-'] = "sub",
		['*'] = "mul", ['/'] = "div",
		['%'] = "mod"
	};
	unsigned int op = expr->op;

	opname = show_special(op);
	if (op < sizeof(name)/sizeof(*name))
		opname = name[op];
	printf("\t%s.%d\t\tv%d,v%d,v%d\n", opname,
		expr->ctype->bit_size,
		new, left, right);
	return new;
}

static int show_regular_preop(struct expression *expr)
{
	int target = show_expression(expr->unop);
	int new = new_pseudo();
	static const char *name[] = {
		['!'] = "nonzero", ['-'] = "neg",
		['~'] = "not",
	};
	unsigned int op = expr->op;
	const char *opname;

	opname = show_special(op);
	if (op < sizeof(name)/sizeof(*name))
		opname = name[op];
	printf("\t%s.%d\t\tv%d,v%d\n", opname, expr->ctype->bit_size, new, target);
	return new;
}

/*
 * FIXME! Not all accesses are memory loads. We should
 * check what kind of symbol is behind the dereference.
 */
static int show_address_gen(struct expression *expr)
{
	if (expr->type == EXPR_PREOP)
		return show_expression(expr->unop);
	return show_expression(expr->address);
}

static int show_load_gen(int bits, struct expression *expr, int addr)
{
	int new = new_pseudo();

	printf("\tld.%d\t\tv%d,[v%d]\n", bits, new, addr);
	if (expr->type == EXPR_PREOP)
		return new;

	/* bitfield load! */
	if (expr->bitpos)
		printf("\tshr.%d\t\tv%d,v%d,$%d\n", bits, new, new, expr->bitpos);
	printf("\tandi.%d\t\tv%d,v%d,$%llu\n", bits, new, new, (1ULL << expr->nrbits)-1);
	return new;
}

static void show_store_gen(int bits, int value, struct expression *expr, int addr)
{
	/* FIXME!!! Bitfield store! */
	printf("\tst.%d\t\tv%d,[v%d]\n", bits, value, addr);
}

static int show_assignment(struct expression *expr)
{
	struct expression *target = expr->left;
	int val, addr, bits = expr->ctype->bit_size;

	val = show_expression(expr->right);
	addr = show_address_gen(target);
	show_store_gen(bits, val, target, addr);
	return val;
}

static int show_access(struct expression *expr)
{
	int addr = show_address_gen(expr);
	return show_load_gen(expr->ctype->bit_size, expr, addr);
}

static int show_inc_dec(struct expression *expr, int postop)
{
	int addr = show_address_gen(expr->unop);
	int retval, new;
	const char *opname = expr->op == SPECIAL_INCREMENT ? "add" : "sub";
	int bits = expr->ctype->bit_size;

	retval = show_load_gen(bits, expr->unop, addr);
	new = retval;
	if (postop)
		new = new_pseudo();
	printf("\t%s.%d\t\tv%d,v%d,$1\n", opname, bits, new, retval);
	show_store_gen(bits, new, expr->unop, addr);
	return retval;
}	

static int show_preop(struct expression *expr)
{
	/*
	 * '*' is an lvalue access, and is fundamentally different
	 * from an arithmetic operation. Maybe it should have an
	 * expression type of its own..
	 */
	if (expr->op == '*')
		return show_access(expr);
	if (expr->op == SPECIAL_INCREMENT || expr->op == SPECIAL_DECREMENT)
		return show_inc_dec(expr, 0);
	return show_regular_preop(expr);
}

static int show_postop(struct expression *expr)
{
	return show_inc_dec(expr, 1);
}	

static int show_symbol_expr(struct symbol *sym)
{
	int new = new_pseudo();
	printf("\tmovi.%d\t\tv%d,$%s\n", BITS_IN_POINTER, new, show_ident(sym->ident));
	return new;
}

static int show_symbol_init(struct symbol *sym)
{
	struct expression *expr = sym->initializer;

	if (expr) {
		int val, addr, bits;

		bits = expr->ctype->bit_size;
		val = show_expression(expr);
		addr = show_symbol_expr(sym);
		show_store_gen(bits, val, NULL, addr);
	}
	return 0;
}

static int type_is_signed(struct symbol *sym)
{
	if (sym->type == SYM_NODE)
		sym = sym->ctype.base_type;
	if (sym->type == SYM_PTR)
		return 0;
	return !(sym->ctype.modifiers & MOD_UNSIGNED);
}

static int show_cast_expr(struct expression *expr)
{
	struct symbol *old_type, *new_type;
	int op = show_expression(expr->cast_expression);
	int oldbits, newbits;
	int new, is_signed;

	old_type = expr->cast_expression->ctype;
	new_type = expr->cast_type;
	
	oldbits = old_type->bit_size;
	newbits = new_type->bit_size;
	if (oldbits >= newbits)
		return op;
	new = new_pseudo();
	is_signed = type_is_signed(old_type);
	if (is_signed) {
		printf("\tsext%d.%d\tv%d,v%d\n", oldbits, newbits, new, op);
	} else {
		printf("\tandl.%d\t\tv%d,v%d,$%lu\n", newbits, new, op, (1UL << oldbits)-1);
	}
	return new;
}

static int show_value(struct expression *expr)
{
	int new = new_pseudo();
	unsigned long long value = expr->value;

	printf("\tmovi.%d\t\tv%d,$%llu\n", expr->ctype->bit_size, new, value);
	return new;
}

static int show_string_expr(struct expression *expr)
{
	int new = new_pseudo();

	printf("\tmovi.%d\t\tv%d,&%s\n", BITS_IN_POINTER, new, show_string(expr->string));
	return new;
}

static int show_bitfield_expr(struct expression *expr)
{
	return show_access(expr);
}

static int show_conditional_expr(struct expression *expr)
{
	int cond = show_expression(expr->conditional);
	int true = show_expression(expr->cond_true);
	int false = show_expression(expr->cond_false);
	int new = new_pseudo();

	if (!true)
		true = cond;
	printf("[v%d]\tcmov.%d\t\tv%d,v%d,v%d\n", cond, expr->ctype->bit_size, new, true, false);
	return new;
}

static int show_statement_expr(struct expression *expr)
{
	return show_statement(expr->statement);
}

static int show_initializer_expr(struct expression *expr)
{
	printf("\t// initializer goes here\n");
	return 0;
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
		return show_symbol_expr(expr->symbol);
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
		return show_initializer_expr(expr);
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


