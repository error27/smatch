/*
 * sparse/compile-i386.c
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 * Copyright 2003 Jeff Garzik
 *
 * Licensed under the Open Software License version 1.1
 *
 * x86 backend
 *
 * TODO list:
 * in general, any non-32bit SYM_BASETYPE is unlikely to work.
 * loops
 * complex initializers
 * bitfields
 * global struct/union variables
 * addressing structures, and members of structures (as opposed to
 *     scalars) on the stack.  Requires smarter stack frame allocation.
 * labels / goto
 * any function argument that isn't 32 bits (or promoted to such)
 * inline asm
 * floating point
 *
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "scope.h"
#include "expression.h"
#include "target.h"


struct textbuf;
struct textbuf {
	unsigned int	len;	/* does NOT include terminating null */
	char		*text;
	struct textbuf	*next;
	struct textbuf	*prev;
};

struct function {
	int stack_size;
	int pseudo_nr;
	struct ptr_list *pseudo_list;
	struct ptr_list *atom_list;
	struct ptr_list *str_list;
	struct symbol **argv;
	unsigned int argc;
	int ret_target;
};

enum storage_type {
	STOR_PSEUDO,	/* variable stored on the stack */
	STOR_ARG,	/* function argument */
	STOR_SYM,	/* a symbol we can directly ref in the asm */
	STOR_REG,	/* scratch register */
	STOR_VALUE,	/* integer constant */
	STOR_LABEL,	/* label / jump target */
	STOR_LABELSYM,	/* label generated from symbol's pointer value */
};

struct reg_info {
	const char	*name;
};

struct storage {
	enum storage_type type;
	unsigned long flags;

	/* STOR_REG */
	struct reg_info *reg;

	union {
		/* STOR_PSEUDO */
		struct {
			int pseudo;
			int offset;
			int size;
		};
		/* STOR_ARG */
		struct {
			int idx;
		};
		/* STOR_SYM */
		struct {
			struct symbol *sym;
		};
		/* STOR_VALUE */
		struct {
			long long value;
		};
		/* STOR_LABEL */
		struct {
			int label;
		};
		/* STOR_LABELSYM */
		struct {
			struct symbol *labelsym;
		};
	};
};

enum {
	STOR_LABEL_VAL	= (1 << 0),
	STOR_WANTS_FREE	= (1 << 1),
};

struct symbol_private {
	struct storage *addr;
};

enum atom_type {
	ATOM_TEXT,
	ATOM_INSN,
	ATOM_CSTR,
};

struct atom {
	enum atom_type type;
	union {
		/* stuff for text */
		struct {
			char *text;
			unsigned int text_len;  /* w/o terminating null */
		};

		/* stuff for insns */
		struct {
			char insn[32];
			char comment[40];
			struct storage *op1;
			struct storage *op2;
		};

		/* stuff for C strings */
		struct {
			struct string *string;
			int label;
		};
	};
};


struct function *current_func = NULL;
struct textbuf *unit_post_text = NULL;
static const char *current_section;

static struct reg_info reg_info_table[] = {
	{ "%eax" },
	{ "%ecx" },
	{ "%edx" },
	{ "%esp" },
	{ "%dl" },
};

static struct storage hardreg_storage_table[] = {
	{	/* eax */
		.type = STOR_REG,
		.reg = &reg_info_table[0],
	},

	{	/* ecx */
		.type = STOR_REG,
		.reg = &reg_info_table[1],
	},

	{	/* edx */
		.type = STOR_REG,
		.reg = &reg_info_table[2],
	},

	{	/* esp */
		.type = STOR_REG,
		.reg = &reg_info_table[3],
	},

	{	/* dl */
		.type = STOR_REG,
		.reg = &reg_info_table[4],
	},
};

#define REG_EAX (&hardreg_storage_table[0])
#define REG_ECX (&hardreg_storage_table[1])
#define REG_EDX (&hardreg_storage_table[2])
#define REG_ESP (&hardreg_storage_table[3])
#define REG_DL	(&hardreg_storage_table[4])


static void emit_move(struct storage *src, struct storage *dest,
		      struct symbol *ctype, const char *comment);
static int type_is_signed(struct symbol *sym);
static struct storage *x86_address_gen(struct expression *expr);
static struct storage *x86_symbol_expr(struct symbol *sym);
static void x86_symbol(struct symbol *sym);
static struct storage *x86_statement(struct statement *stmt);
static struct storage *x86_expression(struct expression *expr);


static inline unsigned int pseudo_offset(struct storage *s)
{
	if (s->type != STOR_PSEUDO)
		return 123456;	/* intentionally bogus value */

	return s->offset;
}

static inline unsigned int arg_offset(struct storage *s)
{
	if (s->type != STOR_ARG)
		return 123456;	/* intentionally bogus value */

	/* FIXME: this is wrong wrong wrong */
	return current_func->stack_size + ((1 + s->idx) * 4);
}

static const char *pretty_offset(int ofs)
{
	static char esp_buf[64];

	if (ofs)
		sprintf(esp_buf, "%d(%%esp)", ofs);
	else
		strcpy(esp_buf, "(%esp)");

	return esp_buf;
}

static void stor_sym_init(struct symbol *sym)
{
	struct storage *stor;
	struct symbol_private *priv;

	priv = calloc(1, sizeof(*priv) + sizeof(*stor));
	if (!priv)
		die("OOM in stor_sym_init");

	stor = (struct storage *) (priv + 1);

	priv->addr = stor;
	stor->type = STOR_SYM;
	stor->sym = sym;
}

static const char *stor_op_name(struct storage *s)
{
	static char name[32];

	switch (s->type) {
	case STOR_PSEUDO:
		strcpy(name, pretty_offset((int) pseudo_offset(s)));
		break;
	case STOR_ARG:
		strcpy(name, pretty_offset((int) arg_offset(s)));
		break;
	case STOR_SYM:
		strcpy(name, show_ident(s->sym->ident));
		break;
	case STOR_REG:
		strcpy(name, s->reg->name);
		break;
	case STOR_VALUE:
		sprintf(name, "$%Ld", s->value);
		break;
	case STOR_LABEL:
		sprintf(name, "%s.L%d", s->flags & STOR_LABEL_VAL ? "$" : "",
			s->label);
		break;
	case STOR_LABELSYM:
		sprintf(name, "%s.LS%p", s->flags & STOR_LABEL_VAL ? "$" : "",
			s->labelsym);
		break;
	}

	return name;
}

static struct atom *new_atom(enum atom_type type)
{
	struct atom *atom;

	atom = calloc(1, sizeof(*atom));	/* TODO: chunked alloc */
	if (!atom)
		die("nuclear OOM");

	atom->type = type;

	return atom;
}

static inline void push_cstring(struct function *f, struct string *str,
				int label)
{
	struct atom *atom;

	atom = new_atom(ATOM_CSTR);
	atom->string = str;
	atom->label = label;

	add_ptr_list(&f->str_list, atom);	/* note: _not_ atom_list */
}

static inline void push_atom(struct function *f, struct atom *atom)
{
	add_ptr_list(&f->atom_list, atom);
}

static void push_text_atom(struct function *f, const char *text)
{
	struct atom *atom = new_atom(ATOM_TEXT);

	atom->text = strdup(text);
	atom->text_len = strlen(text);

	push_atom(f, atom);
}

static struct storage *new_storage(enum storage_type type)
{
	struct storage *stor;

	stor = calloc(1, sizeof(*stor));
	if (!stor)
		die("OOM in new_storage");

	stor->type = type;

	return stor;
}

static struct storage *stack_alloc(int n_bytes)
{
	struct function *f = current_func;
	struct storage *stor;

	assert(f != NULL);

	stor = new_storage(STOR_PSEUDO);
	stor->type = STOR_PSEUDO;
	stor->pseudo = f->pseudo_nr;
	stor->offset = f->stack_size;
	stor->size = n_bytes;
	f->stack_size += n_bytes;
	f->pseudo_nr++;

	add_ptr_list(&f->pseudo_list, stor);

	return stor;
}

static struct storage *new_labelsym(struct symbol *sym)
{
	struct storage *stor;

	stor = new_storage(STOR_LABELSYM);

	if (stor) {
		stor->flags |= STOR_WANTS_FREE;
		stor->labelsym = sym;
	}

	return stor;
}

static struct storage *new_val(long long value)
{
	struct storage *stor;

	stor = new_storage(STOR_VALUE);

	if (stor) {
		stor->flags |= STOR_WANTS_FREE;
		stor->value = value;
	}

	return stor;
}

static int new_label(void)
{
	static int label = 0;
	return ++label;
}

static void textbuf_push(struct textbuf **buf_p, const char *text)
{
	struct textbuf *tmp, *list = *buf_p;
	unsigned int text_len = strlen(text);
	unsigned int alloc_len = text_len + 1 + sizeof(*list);

	tmp = calloc(1, alloc_len);
	if (!tmp)
		die("OOM on textbuf alloc");

	tmp->text = ((void *) tmp) + sizeof(*tmp);
	memcpy(tmp->text, text, text_len + 1);
	tmp->len = text_len;

	/* add to end of list */
	if (!list) {
		list = tmp;
		tmp->prev = tmp;
	} else {
		tmp->prev = list->prev;
		tmp->prev->next = tmp;
		list->prev = tmp;
	}
	tmp->next = list;

	*buf_p = list;
}

static void textbuf_emit(struct textbuf **buf_p)
{
	struct textbuf *tmp, *list = *buf_p;

	while (list) {
		tmp = list;
		if (tmp->next == tmp)
			list = NULL;
		else {
			tmp->prev->next = tmp->next;
			tmp->next->prev = tmp->prev;
			list = tmp->next;
		}

		fputs(tmp->text, stdout);

		free(tmp);
	}

	*buf_p = list;
}

static void insn(const char *insn, struct storage *op1, struct storage *op2,
		 const char *comment_in)
{
	struct function *f = current_func;
	struct atom *atom = new_atom(ATOM_INSN);

	assert(insn != NULL);

	strcpy(atom->insn, insn);
	if (comment_in && (*comment_in))
		strncpy(atom->comment, comment_in,
			sizeof(atom->comment) - 1);

	atom->op1 = op1;
	atom->op2 = op2;

	push_atom(f, atom);
}

static void emit_label (int label, const char *comment)
{
	struct function *f = current_func;
	char s[64];

	if (!comment)
		sprintf(s, ".L%d:\n", label);
	else
		sprintf(s, ".L%d:\t\t\t\t\t# %s\n", label, comment);

	push_text_atom(f, s);
}

static void emit_labelsym (struct symbol *sym, const char *comment)
{
	struct function *f = current_func;
	char s[64];

	if (!comment)
		sprintf(s, ".LS%p:\n", sym);
	else
		sprintf(s, ".LS%p:\t\t\t\t# %s\n", sym, comment);

	push_text_atom(f, s);
}

static void emit_unit_pre(const char *basename)
{
	printf("\t.file\t\"%s\"\n", basename);
}

static void emit_unit_post(void)
{
	textbuf_emit(&unit_post_text);
	printf("\t.ident\t\"sparse silly x86 backend (built %s)\"\n", __DATE__);
}

/* conditionally switch sections */
static void emit_section(const char *s)
{
	if (s == current_section)
		return;
	if (current_section && (!strcmp(s, current_section)))
		return;

	printf("\t%s\n", s);
	current_section = s;
}

static void emit_insn_atom(struct function *f, struct atom *atom)
{
	char s[128];
	char comment[64];
	struct storage *op1 = atom->op1;
	struct storage *op2 = atom->op2;

	if (atom->comment[0])
		sprintf(comment, "\t\t# %s", atom->comment);
	else
		comment[0] = 0;

	if (atom->op2) {
		char tmp[16];
		strcpy(tmp, stor_op_name(op1));
		sprintf(s, "\t%s\t%s, %s%s\n",
			atom->insn, tmp, stor_op_name(op2), comment);
	} else if (atom->op1)
		sprintf(s, "\t%s\t%s%s%s\n",
			atom->insn, stor_op_name(op1),
			comment[0] ? "\t" : "", comment);
	else
		sprintf(s, "\t%s\t%s%s\n",
			atom->insn,
			comment[0] ? "\t\t" : "", comment);

	write(STDOUT_FILENO, s, strlen(s));
}

static void emit_atom_list(struct function *f)
{
	struct atom *atom;

	FOR_EACH_PTR(f->atom_list, atom) {
		switch (atom->type) {
		case ATOM_TEXT: {
			ssize_t rc = write(STDOUT_FILENO, atom->text,
					   atom->text_len);
			(void) rc;	/* FIXME */
			break;
		}
		case ATOM_INSN:
			emit_insn_atom(f, atom);
			break;
		case ATOM_CSTR:
			assert(0);
			break;
		}
	} END_FOR_EACH_PTR;
}

static void emit_string_list(struct function *f)
{
	struct atom *atom;

	emit_section(".section\t.rodata");

	FOR_EACH_PTR(f->str_list, atom) {
		/* FIXME: escape " in string */
		printf(".L%d:\n", atom->label);
		printf("\t.string\t%s\n", show_string(atom->string));

		free(atom);
	} END_FOR_EACH_PTR;
}

static void func_cleanup(struct function *f)
{
	struct storage *stor;
	struct atom *atom;

	FOR_EACH_PTR(f->pseudo_list, stor) {
		free(stor);
	} END_FOR_EACH_PTR;

	FOR_EACH_PTR(f->atom_list, atom) {
		if ((atom->type == ATOM_TEXT) && (atom->text))
			free(atom->text);
		if (atom->op1 && (atom->op1->flags & STOR_WANTS_FREE))
			free(atom->op1);
		if (atom->op2 && (atom->op2->flags & STOR_WANTS_FREE))
			free(atom->op2);
		free(atom);
	} END_FOR_EACH_PTR;

	free_ptr_list(&f->pseudo_list);
	free(f);
}

/* function prologue */
static void emit_func_pre(struct symbol *sym)
{
	struct function *f;
	struct symbol *arg;
	unsigned int i, argc = 0, alloc_len;
	unsigned char *mem;
	struct symbol_private *privbase;
	struct storage *storage_base;
	struct symbol *base_type = sym->ctype.base_type;

	FOR_EACH_PTR(base_type->arguments, arg) {
		argc++;
	} END_FOR_EACH_PTR;

	alloc_len =
		sizeof(*f) +
		(argc * sizeof(struct symbol *)) +
		(argc * sizeof(struct symbol_private)) +
		(argc * sizeof(struct storage));
	mem = calloc(1, alloc_len);
	if (!mem)
		die("OOM on func info");

	f		=  (struct function *) mem;
	mem		+= sizeof(*f);
	f->argv		=  (struct symbol **) mem;
	mem		+= (argc * sizeof(struct symbol *));
	privbase	=  (struct symbol_private *) mem;
	mem		+= (argc * sizeof(struct symbol_private));
	storage_base	=  (struct storage *) mem;

	f->argc = argc;
	f->ret_target = new_label();

	i = 0;
	FOR_EACH_PTR(base_type->arguments, arg) {
		f->argv[i] = arg;
		arg->aux = &privbase[i];
		storage_base[i].type = STOR_ARG;
		storage_base[i].idx = i;
		privbase[i].addr = &storage_base[i];
		i++;
	} END_FOR_EACH_PTR;

	assert(current_func == NULL);
	current_func = f;
}

/* function epilogue */
static void emit_func_post(struct symbol *sym)
{
	const char *name = show_ident(sym->ident);
	struct function *f = current_func;
	int stack_size = f->stack_size;

	if (f->str_list)
		emit_string_list(f);

	/* function prologue */
	emit_section(".text");
	if ((sym->ctype.modifiers & MOD_STATIC) == 0)
		printf(".globl %s\n", name);
	printf("\t.type\t%s, @function\n", name);
	printf("%s:\n", name);

	if (stack_size) {
		char pseudo_const[16];

		sprintf(pseudo_const, "$%d", stack_size);
		printf("\tsubl\t%s, %%esp\n", pseudo_const);
	}

	/* function epilogue */

	/* jump target for 'return' statements */
	emit_label(f->ret_target, NULL);

	if (stack_size) {
		struct storage *val;

		val = new_storage(STOR_VALUE);
		val->value = (long long) (stack_size);
		val->flags = STOR_WANTS_FREE;

		insn("addl", val, REG_ESP, NULL);
	}

	insn("ret", NULL, NULL, NULL);

	/* output everything to stdout */
	fflush(stdout);		/* paranoia; needed? */
	emit_atom_list(f);

	/* function footer */
	printf("\t.size\t%s, .-%s\n", name, name);

	func_cleanup(f);
	current_func = NULL;
}

/* emit object (a.k.a. variable, a.k.a. data) prologue */
static void emit_object_pre(const char *name, unsigned long modifiers,
			    unsigned long alignment, unsigned int byte_size)
{
	if ((modifiers & MOD_STATIC) == 0)
		printf(".globl %s\n", name);
	emit_section(".data");
	if (alignment)
		printf("\t.align %lu\n", alignment);
	printf("\t.type\t%s, @object\n", name);
	printf("\t.size\t%s, %d\n", name, byte_size);
	printf("%s:\n", name);
}

/* emit value (only) for an initializer scalar */
static void emit_scalar(struct expression *expr, unsigned int bit_size)
{
	const char *type;
	long long ll;

	assert(expr->type == EXPR_VALUE);

	if (expr->value == 0ULL) {
		printf("\t.zero\t%d\n", bit_size / 8);
		return;
	}

	ll = (long long) expr->value;

	switch (bit_size) {
	case 8:		type = "byte";	ll = (char) ll; break;
	case 16:	type = "value";	ll = (short) ll; break;
	case 32:	type = "long";	ll = (int) ll; break;
	case 64:	type = "quad";	break;
	default:	type = NULL;	break;
	}

	assert(type != NULL);

	printf("\t.%s\t%Ld\n", type, ll);
}

static void emit_global_noinit(const char *name, unsigned long modifiers,
			       unsigned long alignment, unsigned int byte_size)
{
	char s[64];

	if (modifiers & MOD_STATIC) {
		sprintf(s, "\t.local\t%s\n", name);
		textbuf_push(&unit_post_text, s);
	}
	if (alignment)
		sprintf(s, "\t.comm\t%s,%d,%lu\n", name, byte_size, alignment);
	else
		sprintf(s, "\t.comm\t%s,%d\n", name, byte_size);
	textbuf_push(&unit_post_text, s);
}

static int ea_current, ea_last;

static void emit_initializer(struct symbol *sym,
			     struct expression *expr)
{
	int distance = ea_current - ea_last - 1;

	if (distance > 0)
		printf("\t.zero\t%d\n", (sym->bit_size / 8) * distance);

	if (expr->type == EXPR_VALUE) {
		struct symbol *base_type = sym->ctype.base_type;
		assert(base_type != NULL);

		emit_scalar(expr, sym->bit_size / base_type->array_size);
		return;
	}
	if (expr->type != EXPR_INITIALIZER)
		return;

	assert(0); /* FIXME */
}

static int sort_array_cmp(const struct expression *a,
			  const struct expression *b)
{
	int a_ofs = 0, b_ofs = 0;

	if (a->type == EXPR_POS)
		a_ofs = (int) a->init_offset;
	if (b->type == EXPR_POS)
		b_ofs = (int) b->init_offset;

	return a_ofs - b_ofs;
}

/* move to front-end? */
static void sort_array(struct expression *expr)
{
	struct expression *entry, **list;
	unsigned int elem, sorted, i;

	elem = 0;
	FOR_EACH_PTR(expr->expr_list, entry) {
		elem++;
	} END_FOR_EACH_PTR;

	if (!elem)
		return;

	list = malloc(sizeof(entry) * elem);
	if (!list)
		die("OOM in sort_array");

	/* this code is no doubt evil and ignores EXPR_INDEX possibly
	 * to its detriment and other nasty things.  improvements
	 * welcome.
	 */
	i = 0;
	sorted = 0;
	FOR_EACH_PTR(expr->expr_list, entry) {
		if ((entry->type == EXPR_POS) || (entry->type == EXPR_VALUE)) {
			/* add entry to list[], in sorted order */
			if (sorted == 0) {
				list[0] = entry;
				sorted = 1;
			} else {
				unsigned int i;

				for (i = 0; i < sorted; i++)
					if (sort_array_cmp(entry, list[i]) <= 0)
						break;

				/* If inserting into the middle of list[]
				 * instead of appending, we memmove.
				 * This is ugly, but thankfully
				 * uncommon.  Input data with tons of
				 * entries very rarely have explicit
				 * offsets.  convert to qsort eventually...
				 */
				if (i != sorted)
					memmove(&list[i + 1], &list[i],
						(sorted - i) * sizeof(entry));
				list[i] = entry;
				sorted++;
			}
		}
	} END_FOR_EACH_PTR;

	i = 0;
	FOR_EACH_PTR(expr->expr_list, entry) {
		if ((entry->type == EXPR_POS) || (entry->type == EXPR_VALUE))
			__list->list[__i] = list[i++];
	} END_FOR_EACH_PTR;

}

static void emit_array(struct symbol *sym)
{
	struct symbol *base_type = sym->ctype.base_type;
	struct expression *expr = sym->initializer;
	struct expression *entry;

	assert(base_type != NULL);

	stor_sym_init(sym);

	ea_last = -1;

	emit_object_pre(show_ident(sym->ident), sym->ctype.modifiers,
		        sym->ctype.alignment,
			sym->bit_size / 8);

	sort_array(expr);

	FOR_EACH_PTR(expr->expr_list, entry) {
		if (entry->type == EXPR_VALUE) {
			ea_current = 0;
			emit_initializer(sym, entry);
			ea_last = ea_current;
		} else if (entry->type == EXPR_POS) {
			ea_current =
			    entry->init_offset / (base_type->bit_size / 8);
			emit_initializer(sym, entry->init_expr);
			ea_last = ea_current;
		}
	} END_FOR_EACH_PTR;
}

static void emit_one_symbol(struct symbol *sym, void *dummy, int flags)
{
	x86_symbol(sym);
}

void emit_unit(const char *basename, struct symbol_list *list)
{
	emit_unit_pre(basename);
	symbol_iterate(list, emit_one_symbol, NULL);
	emit_unit_post();
}

static void emit_copy(struct storage *src,  struct symbol *src_ctype,
		      struct storage *dest, struct symbol *dest_ctype)
{
	/* FIXME: Bitfield move! */

	emit_move(src, REG_EAX, src_ctype, "begin copy ..");
	emit_move(REG_EAX, dest, dest_ctype, ".... end copy");
}

static void emit_store(struct expression *dest_expr, struct storage *dest,
		       struct storage *src, int bits)
{
	/* FIXME: Bitfield store! */
	printf("\tst.%d\t\tv%d,[v%d]\n", bits, src->pseudo, dest->pseudo);
}

static void emit_scalar_noinit(struct symbol *sym)
{
	emit_global_noinit(show_ident(sym->ident),
			   sym->ctype.modifiers, sym->ctype.alignment,
			   sym->bit_size / 8);
	stor_sym_init(sym);
}

static void emit_array_noinit(struct symbol *sym)
{
	emit_global_noinit(show_ident(sym->ident),
			   sym->ctype.modifiers, sym->ctype.alignment,
			   sym->array_size * (sym->bit_size / 8));
	stor_sym_init(sym);
}

static const char *opbits(const char *insn, unsigned int bits)
{
	static char opbits_str[32];
	char c;

	switch (bits) {
	case 8:	 c = 'b'; break;
	case 16: c = 'w'; break;
	case 32: c = 'l'; break;
	case 64: c = 'q'; break;
	default: assert(0); break;
	}

	sprintf(opbits_str, "%s%c", insn, bits);

	return opbits_str;
}

static void emit_move(struct storage *src, struct storage *dest,
		      struct symbol *ctype, const char *comment)
{
	unsigned int bits;
	unsigned int is_signed;
	unsigned int is_dest = (src->type == STOR_REG);
	const char *opname;

	if (ctype) {
		bits = ctype->bit_size;
		is_signed = type_is_signed(ctype);
	} else {
		bits = 32;
		is_signed = 0;
	}

	if ((dest->type == STOR_REG) && (src->type == STOR_REG)) {
		insn("mov", src, dest, NULL);
		return;
	}

	switch (bits) {
	case 8:
		if (is_dest)
					opname = "movb";
		else {
			if (is_signed)	opname = "movsxb";
			else		opname = "movzxb";
		}
		break;
	case 16:
		if (is_dest)
					opname = "movw";
		else {
			if (is_signed)	opname = "movsxw";
			else		opname = "movzxw";
		}
		break;

	case 32:			opname = "movl";	break;
	case 64:			opname = "movq";	break;

	default:			assert(0);		break;
	}

	insn(opname, src, dest, comment);
}

static struct storage *emit_compare(struct expression *expr)
{
	struct storage *left = x86_expression(expr->left);
	struct storage *right = x86_expression(expr->right);
	struct storage *new, *val;
	const char *opname = NULL;
	unsigned int is_signed = type_is_signed(expr->left->ctype); /* FIXME */
	unsigned int right_bits = expr->right->ctype->bit_size;

	switch(expr->op) {
	case '<':
		if (is_signed)	opname = "setl";
		else		opname = "setb";
		break;
	case '>':
		if (is_signed)	opname = "setg";
		else		opname = "seta";
		break;
	case SPECIAL_LTE:
		if (is_signed)	opname = "setle";
		else		opname = "setbe";
		break;
	case SPECIAL_GTE:
		if (is_signed)	opname = "setge";
		else		opname = "setae";
		break;

	case SPECIAL_EQUAL:	opname = "sete";	break;
	case SPECIAL_NOTEQUAL:	opname = "setne";	break;

	default:
		assert(0);
		break;
	}

	/* init EDX to 0 */
	val = new_storage(STOR_VALUE);
	val->flags = STOR_WANTS_FREE;
	emit_move(val, REG_EDX, NULL, NULL);

	/* move op1 into EAX */
	emit_move(left, REG_EAX, expr->left->ctype, NULL);

	/* perform comparison, RHS (op1, right) and LHS (op2, EAX) */
	insn(opbits("cmp", right_bits), right, REG_EAX, NULL);

	/* store result of operation, 0 or 1, in DL using SETcc */
	insn(opname, REG_DL, NULL, NULL);

	/* finally, store the result (DL) in a new pseudo / stack slot */
	new = stack_alloc(4);
	emit_move(REG_EDX, new, NULL, "end EXPR_COMPARE");

	return new;
}

static struct storage *emit_value(struct expression *expr)
{
#if 0 /* old and slow way */
	struct storage *new = stack_alloc(4);
	struct storage *val;

	val = new_storage(STOR_VALUE);
	val->value = (long long) expr->value;
	val->flags = STOR_WANTS_FREE;
	insn("movl", val, new, NULL);

	return new;
#else
	struct storage *val;

	val = new_storage(STOR_VALUE);
	val->value = (long long) expr->value;

	return val;	/* FIXME: memory leak */
#endif
}

static struct storage *emit_binop(struct expression *expr)
{
	struct storage *left = x86_expression(expr->left);
	struct storage *right = x86_expression(expr->right);
	struct storage *new;
	const char *opname = NULL;

	/*
	 * FIXME FIXME this routine is so wrong it's not even funny.
	 * On x86 both mod/div are handled with the same instruction.
	 * We don't pay attention to signed/unsigned issues,
	 * and like elsewhere we hardcode the operand size at 32 bits.
	 */

	switch (expr->op) {
	case '+':			opname = "addl";	break;
	case '-':			opname = "subl";	break;
	case '*':			opname = "mull";	break;
	case '/':			opname = "divl";	break;
	case '%':			opname = "modl";	break;
	case '&':			opname = "andl";	break;
	case '|':			opname = "orl";		break;
	case '^':			opname = "xorl";	break;
	case SPECIAL_LEFTSHIFT:		opname = "shll";	break;
	case SPECIAL_RIGHTSHIFT:	opname = "shrl";	break;
	default:			assert(0);		break;
	}

	/* load op2 into EAX */
	insn("movl", right, REG_EAX, "EXPR_BINOP/COMMA/LOGICAL");

	/* perform binop */
	insn(opname, left, REG_EAX, NULL);

	/* store result (EAX) in new pseudo / stack slot */
	new = stack_alloc(4);
	insn("movl", REG_EAX, new, "end EXPR_BINOP");

	return new;
}

static void emit_if_conditional(struct statement *stmt)
{
	struct storage *val, *target_val;
	int target;
	struct expression *cond = stmt->if_conditional;

/* This is only valid if nobody can jump into the "dead" statement */
#if 0
	if (cond->type == EXPR_VALUE) {
		struct statement *s = stmt->if_true;
		if (!cond->value)
			s = stmt->if_false;
		x86_statement(s);
		break;
	}
#endif
	val = x86_expression(cond);

	/* load 'if' test result into EAX */
	insn("movl", val, REG_EAX, "begin if conditional");

	/* compare 'if' test result */
	insn("test", REG_EAX, REG_EAX, NULL);

	/* create end-of-if label / if-failed labelto jump to,
	 * and jump to it if the expression returned zero.
	 */
	target = new_label();
	target_val = new_storage(STOR_LABEL);
	target_val->label = target;
	target_val->flags = STOR_WANTS_FREE;
	insn("jz", target_val, NULL, NULL);

	x86_statement(stmt->if_true);
	if (stmt->if_false) {
		struct storage *last_val;
		int last;

		/* finished generating code for if-true statement.
		 * add a jump-to-end jump to avoid falling through
		 * to the if-false statement code.
		 */
		last = new_label();
		last_val = new_storage(STOR_LABEL);
		last_val->label = last;
		last_val->flags = STOR_WANTS_FREE;
		insn("jmp", last_val, NULL, NULL);

		/* if we have both if-true and if-false statements,
		 * the failed-conditional case will fall through to here
		 */
		emit_label(target, NULL);

		target = last;
		x86_statement(stmt->if_false);
	}

	emit_label(target, "end if");
}

static struct storage *emit_inc_dec(struct expression *expr, int postop)
{
	struct storage *addr = x86_address_gen(expr->unop);
	struct storage *retval;
	char opname[16];

	strcpy(opname, opbits(expr->op == SPECIAL_INCREMENT ? "inc" : "dec",
			      expr->ctype->bit_size));

	if (postop) {
		struct storage *new = stack_alloc(4);

		emit_copy(addr, expr->unop->ctype, new, NULL);

		retval = new;
	} else
		retval = addr;

	insn(opname, addr, NULL, NULL);

	return retval;
}

static struct storage *emit_postop(struct expression *expr)
{
	return emit_inc_dec(expr, 1);
}

static struct storage *emit_return_stmt(struct statement *stmt)
{
	struct function *f = current_func;
	struct expression *expr = stmt->ret_value;
	struct storage *val = NULL, *jmplbl;

	if (expr && expr->ctype) {
		val = x86_expression(expr);
		assert(val != NULL);
		emit_move(val, REG_EAX, expr->ctype, "return");
	}

	jmplbl = new_storage(STOR_LABEL);
	jmplbl->flags |= STOR_WANTS_FREE;
	jmplbl->label = f->ret_target;
	insn("jmp", jmplbl, NULL, NULL);

	return val;
}

static struct storage *emit_conditional_expr(struct expression *expr)
{
	struct storage *cond = x86_expression(expr->conditional);
	struct storage *true = x86_expression(expr->cond_true);
	struct storage *false = x86_expression(expr->cond_false);
	struct storage *new = stack_alloc(4);

	if (!true)
		true = cond;

	emit_move(cond,  REG_EAX, expr->conditional->ctype,
		  "begin EXPR_CONDITIONAL");
	emit_move(true,  REG_ECX, expr->cond_true->ctype,   NULL);
	emit_move(false, REG_EDX, expr->cond_false->ctype,  NULL);

	/* test EAX (for zero/non-zero) */
	insn("test", REG_EAX, REG_EAX, NULL);

	/* if false, move EDX to ECX */
	insn("cmovz", REG_EDX, REG_ECX, NULL);

	/* finally, store the result (ECX) in a new pseudo / stack slot */
	new = stack_alloc(4);
	emit_move(REG_ECX, new, expr->ctype, "end EXPR_CONDITIONAL");
	/* FIXME: we lose type knowledge of expression result at this point */

	return new;
}

static struct storage *emit_symbol_expr_init(struct symbol *sym)
{
	struct expression *expr = sym->initializer;
	struct symbol_private *priv = sym->aux;

	if (priv == NULL) {
		priv = calloc(1, sizeof(*priv));
		sym->aux = priv;

		if (expr == NULL) {
			struct storage *new = stack_alloc(4);
			fprintf(stderr, "FIXME! no value for symbol.  creating pseudo %d (stack offset %d)\n",
				new->pseudo, new->pseudo * 4);
			priv->addr = new;
		} else {
			priv->addr = x86_expression(expr);
		}
	}

	return priv->addr;
}

static struct storage *emit_string_expr(struct expression *expr)
{
	struct function *f = current_func;
	int label = new_label();
	struct storage *new;

	push_cstring(f, expr->string, label);

	new = new_storage(STOR_LABEL);
	new->label = label;
	new->flags = STOR_LABEL_VAL | STOR_WANTS_FREE;
	return new;
}

static struct storage *emit_cast_expr(struct expression *expr)
{
	struct symbol *old_type, *new_type;
	struct storage *op = x86_expression(expr->cast_expression);
	int oldbits, newbits;
	struct storage *new;

	old_type = expr->cast_expression->ctype;
	new_type = expr->cast_type;

	oldbits = old_type->bit_size;
	newbits = new_type->bit_size;
	if (oldbits >= newbits)
		return op;

	emit_move(op, REG_EAX, old_type, "begin cast ..");

	new = stack_alloc(4);
	emit_move(REG_EAX, new, new_type, ".... end cast");

	return new;
}

static struct storage *emit_regular_preop(struct expression *expr)
{
	struct storage *target = x86_expression(expr->unop);
	struct storage *val, *new = stack_alloc(4);
	const char *opname = NULL;

	switch (expr->op) {
	case '!':
		val = new_storage(STOR_VALUE);
		val->flags = STOR_WANTS_FREE;
		emit_move(val, REG_EDX, NULL, NULL);
		emit_move(target, REG_EAX, expr->unop->ctype, NULL);
		insn("test", REG_EAX, REG_EAX, NULL);
		insn("setz", REG_DL, NULL, NULL);
		emit_move(REG_EDX, new, expr->unop->ctype, NULL);

		break;
	case '~':
		opname = "not";
	case '-':
		if (!opname)
			opname = "neg";
		emit_move(target, REG_EAX, expr->unop->ctype, NULL);
		insn(opname, REG_EAX, NULL, NULL);
		emit_move(REG_EAX, new, expr->unop->ctype, NULL);
		break;
	default:
		assert(0);
		break;
	}

	return new;
}

static void emit_case_statement(struct statement *stmt)
{
	emit_labelsym(stmt->case_label, NULL);
	x86_statement(stmt->case_statement);
}

static void emit_switch_statement(struct statement *stmt)
{
	struct storage *val = x86_expression(stmt->switch_expression);
	struct symbol *sym, *default_sym = NULL;
	struct storage *labelsym, *label;
	int switch_end = 0;

	emit_move(val, REG_EAX, stmt->switch_expression->ctype, "begin case");

	/*
	 * This is where a _real_ back-end would go through the
	 * cases to decide whether to use a lookup table or a
	 * series of comparisons etc
	 */
	FOR_EACH_PTR(stmt->switch_case->symbol_list, sym) {
		struct statement *case_stmt = sym->stmt;
		struct expression *expr = case_stmt->case_expression;
		struct expression *to = case_stmt->case_to;

		/* default: */
		if (!expr)
			default_sym = sym;

		/* case NNN: */
		else {
			struct storage *case_val = new_val(expr->value);

			assert (expr->type == EXPR_VALUE);

			insn("cmpl", case_val, REG_EAX, NULL);

			if (!to) {
				labelsym = new_labelsym(sym);
				insn("je", labelsym, NULL, NULL);
			} else {
				int next_test;

				label = new_storage(STOR_LABEL);
				label->flags |= STOR_WANTS_FREE;
				label->label = next_test = new_label();
				
				/* FIXME: signed/unsigned */
				insn("jl", label, NULL, NULL);

				case_val = new_val(to->value);
				insn("cmpl", case_val, REG_EAX, NULL);

				/* TODO: implement and use refcounting... */
				label = new_storage(STOR_LABEL);
				label->flags |= STOR_WANTS_FREE;
				label->label = next_test;
				
				/* FIXME: signed/unsigned */
				insn("jg", label, NULL, NULL);

				labelsym = new_labelsym(sym);
				insn("jmp", labelsym, NULL, NULL);

				emit_label(next_test, NULL);
			}
		}
	} END_FOR_EACH_PTR;

	if (default_sym) {
		labelsym = new_labelsym(default_sym);
		insn("jmp", labelsym, NULL, "default");
	} else {
		label = new_storage(STOR_LABEL);
		label->flags |= STOR_WANTS_FREE;
		label->label = switch_end = new_label();
		insn("jmp", label, NULL, "goto end of switch");
	}

	x86_statement(stmt->switch_statement);

	if (stmt->switch_break->used)
		emit_labelsym(stmt->switch_break, NULL);

	if (switch_end)
		emit_label(switch_end, NULL);
}

static void x86_struct_member(struct symbol *sym, void *data, int flags)
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

static void x86_symbol(struct symbol *sym)
{
	struct symbol *type;

	if (!sym)
		return;

	type = sym->ctype.base_type;
	if (!type)
		return;

	/*
	 * Show actual implementation information
	 */
	switch (type->type) {

	case SYM_ARRAY:
		if (sym->initializer)
			emit_array(sym);
		else
			emit_array_noinit(sym);
		break;

	case SYM_BASETYPE:
		if (sym->initializer) {
			emit_object_pre(show_ident(sym->ident),
					sym->ctype.modifiers,
				        sym->ctype.alignment,
					sym->bit_size / 8);
			emit_scalar(sym->initializer, sym->bit_size);
			stor_sym_init(sym);
		} else
			emit_scalar_noinit(sym);
		break;

	case SYM_STRUCT:
		symbol_iterate(type->symbol_list, x86_struct_member, NULL);
		break;

	case SYM_UNION:
		symbol_iterate(type->symbol_list, x86_struct_member, NULL);
		break;

	case SYM_FN: {
		struct statement *stmt = type->stmt;
		if (stmt) {
			emit_func_pre(sym);
			x86_statement(stmt);
			emit_func_post(sym);
		}
		break;
	}

	default:
		break;
	}

	if (sym->initializer && (type->type != SYM_BASETYPE) &&
	    (type->type != SYM_ARRAY)) {
		printf(" = \n");
		x86_expression(sym->initializer);
	}
}

static void x86_symbol_init(struct symbol *sym);

static void x86_symbol_decl(struct symbol_list *syms)
{
	struct symbol *sym;
	FOR_EACH_PTR(syms, sym) {
		x86_symbol_init(sym);
	} END_FOR_EACH_PTR;
}

static void x86_loop(struct statement *stmt)
{
	struct statement  *pre_statement = stmt->iterator_pre_statement;
	struct expression *pre_condition = stmt->iterator_pre_condition;
	struct statement  *statement = stmt->iterator_statement;
	struct statement  *post_statement = stmt->iterator_post_statement;
	struct expression *post_condition = stmt->iterator_post_condition;
	int loop_top = 0, loop_bottom = 0;
	struct storage *val;

	x86_symbol_decl(stmt->iterator_syms);
	x86_statement(pre_statement);
	if (pre_condition) {
		if (pre_condition->type == EXPR_VALUE) {
			if (!pre_condition->value) {
				loop_bottom = new_label();
				printf("\tjmp\t\t.L%d\n", loop_bottom);
			}
		} else {
			loop_bottom = new_label();
			val = x86_expression(pre_condition);
			printf("\tje\t\tv%d, .L%d\n", val->pseudo, loop_bottom);
		}
	}
	if (!post_condition || post_condition->type != EXPR_VALUE || post_condition->value) {
		loop_top = new_label();
		printf(".L%d:\n", loop_top);
	}
	x86_statement(statement);
	if (stmt->iterator_continue->used)
		printf(".L%p:\n", stmt->iterator_continue);
	x86_statement(post_statement);
	if (!post_condition) {
		printf("\tjmp\t\t.L%d\n", loop_top);
	} else if (post_condition->type == EXPR_VALUE) {
		if (post_condition->value)
			printf("\tjmp\t\t.L%d\n", loop_top);
	} else {
		val = x86_expression(post_condition);
		printf("\tjne\t\tv%d, .L%d\n", val->pseudo, loop_top);
	}
	if (stmt->iterator_break->used)
		printf(".L%p:\n", stmt->iterator_break);
	if (loop_bottom)
		printf(".L%d:\n", loop_bottom);
}

/*
 * Print out a statement
 */
static struct storage *x86_statement(struct statement *stmt)
{
	if (!stmt)
		return 0;
	switch (stmt->type) {
	case STMT_RETURN:
		return emit_return_stmt(stmt);
	case STMT_COMPOUND: {
		struct statement *s;
		struct storage *last = NULL;

		x86_symbol_decl(stmt->syms);
		FOR_EACH_PTR(stmt->stmts, s) {
			last = x86_statement(s);
		} END_FOR_EACH_PTR;

		return last;
	}

	case STMT_EXPRESSION:
		return x86_expression(stmt->expression);
	case STMT_IF:
		emit_if_conditional(stmt);
		return NULL;

	case STMT_CASE:
		emit_case_statement(stmt);
		break;
	case STMT_SWITCH:
		emit_switch_statement(stmt);
		break;

	case STMT_ITERATOR:
		x86_loop(stmt);
		break;

	case STMT_NONE:
		break;

	case STMT_LABEL:
		printf(".L%p:\n", stmt->label_identifier);
		x86_statement(stmt->label_statement);
		break;

	case STMT_GOTO:
		if (stmt->goto_expression) {
			struct storage *val = x86_expression(stmt->goto_expression);
			printf("\tgoto *v%d\n", val->pseudo);
		} else {
			struct storage *labelsym = new_labelsym(stmt->goto_label);
			insn("jmp", labelsym, NULL, NULL);
		}
		break;
	case STMT_ASM:
		printf("\tasm( .... )\n");
		break;

	}
	return NULL;
}

static struct storage *x86_call_expression(struct expression *expr)
{
	struct function *f = current_func;
	struct symbol *direct;
	struct expression *arg, *fn;
	struct storage *retval, *fncall;
	int framesize;
	char s[64];

	if (!expr->ctype) {
		warn(expr->pos, "\tcall with no type!");
		return NULL;
	}

	framesize = 0;
	FOR_EACH_PTR_REVERSE(expr->args, arg) {
		struct storage *new = x86_expression(arg);
		int size = arg->ctype->bit_size;

		/* FIXME: pay attention to 'size' */
		insn("pushl", new, NULL,
		     !framesize ? "begin function call" : NULL);

		framesize += size >> 3;
	} END_FOR_EACH_PTR_REVERSE;

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
		struct storage *direct_stor = new_storage(STOR_SYM);
		direct_stor->flags |= STOR_WANTS_FREE;
		direct_stor->sym = direct;
		insn("call", direct_stor, NULL, NULL);
	} else {
		fncall = x86_expression(fn);
		emit_move(fncall, REG_EAX, fn->ctype, NULL);

		strcpy(s, "\tcall\t*%eax\n");
		push_text_atom(f, s);
	}

	/* FIXME: pay attention to BITS_IN_POINTER */
	if (framesize) {
		struct storage *val = new_storage(STOR_VALUE);
		val->value = (long long) framesize;
		val->flags = STOR_WANTS_FREE;
		insn("addl", val, REG_ESP, NULL);
	}

	retval = stack_alloc(4);
	emit_move(REG_EAX, retval, NULL, "end function call");

	return retval;
}

static struct storage *x86_address_gen(struct expression *expr)
{
	struct function *f = current_func;
	struct storage *addr;
	struct storage *new;
	char s[32];

	if ((expr->type != EXPR_PREOP) || (expr->op != '*'))
		return x86_expression(expr->address);

	addr = x86_expression(expr->unop);
	if (expr->unop->type == EXPR_SYMBOL)
		return addr;

	emit_move(addr, REG_EAX, NULL, "begin deref ..");

	/* FIXME: operand size */
	strcpy(s, "\tmovl\t(%eax), %ecx\n");
	push_text_atom(f, s);

	new = stack_alloc(4);
	emit_move(REG_ECX, new, NULL, ".... end deref");

	return new;
}

static struct storage *x86_assignment(struct expression *expr)
{
	struct expression *target = expr->left;
	struct storage *val, *addr;
	int bits;

	if (!expr->ctype)
		return NULL;

	bits = expr->ctype->bit_size;
	val = x86_expression(expr->right);
	addr = x86_address_gen(target);

	switch (val->type) {
	/* copy, where both operands are memory */
	case STOR_PSEUDO:
	case STOR_ARG:
		emit_copy(val, expr->right->ctype, addr, expr->left->ctype);
		break;

	/* copy, one or zero operands are memory */
	case STOR_REG:
	case STOR_SYM:
	case STOR_VALUE:
	case STOR_LABEL:
		emit_move(val, addr, expr->left->ctype, NULL);
		break;

	case STOR_LABELSYM:
		assert(0);
		break;
	}
	return val;
}

static int x86_initialization(struct symbol *sym, struct expression *expr)
{
	struct storage *val, *addr;
	int bits;

	if (!expr->ctype)
		return 0;

	bits = expr->ctype->bit_size;
	val = x86_expression(expr);
	addr = x86_symbol_expr(sym);
	// FIXME! The "target" expression is for bitfield store information.
	// Leave it NULL, which works fine.
	emit_store(NULL, addr, val, bits);
	return 0;
}

static struct storage *x86_access(struct expression *expr)
{
	return x86_address_gen(expr);
}

static struct storage *x86_preop(struct expression *expr)
{
	/*
	 * '*' is an lvalue access, and is fundamentally different
	 * from an arithmetic operation. Maybe it should have an
	 * expression type of its own..
	 */
	if (expr->op == '*')
		return x86_access(expr);
	if (expr->op == SPECIAL_INCREMENT || expr->op == SPECIAL_DECREMENT)
		return emit_inc_dec(expr, 0);
	return emit_regular_preop(expr);
}

static struct storage *x86_symbol_expr(struct symbol *sym)
{
	struct storage *new = stack_alloc(4);

	if (sym->ctype.modifiers & (MOD_TOPLEVEL | MOD_EXTERN | MOD_STATIC)) {
		printf("\tmovi.%d\t\tv%d,$%s\n", BITS_IN_POINTER, new->pseudo, show_ident(sym->ident));
		return new;
	}
	if (sym->ctype.modifiers & MOD_ADDRESSABLE) {
		printf("\taddi.%d\t\tv%d,vFP,$%lld\n", BITS_IN_POINTER, new->pseudo, sym->value);
		return new;
	}
	printf("\taddi.%d\t\tv%d,vFP,$offsetof(%s:%p)\n", BITS_IN_POINTER, new->pseudo, show_ident(sym->ident), sym);
	return new;
}

static void x86_symbol_init(struct symbol *sym)
{
	struct symbol_private *priv = sym->aux;
	struct expression *expr = sym->initializer;
	struct storage *new;

	if (expr)
		new = x86_expression(expr);
	else
		new = stack_alloc(sym->bit_size / 8);

	if (!priv) {
		priv = calloc(1, sizeof(*priv));
		sym->aux = priv;
		/* FIXME: leak! we don't free... */
		/* (well, we don't free symbols either) */
	}

	priv->addr = new;
}

static int type_is_signed(struct symbol *sym)
{
	if (sym->type == SYM_NODE)
		sym = sym->ctype.base_type;
	if (sym->type == SYM_PTR)
		return 0;
	return !(sym->ctype.modifiers & MOD_UNSIGNED);
}

static struct storage *x86_bitfield_expr(struct expression *expr)
{
	return x86_access(expr);
}

static struct storage *x86_label_expr(struct expression *expr)
{
	struct storage *new = stack_alloc(4);
	printf("\tmovi.%d\t\tv%d,.L%p\n",BITS_IN_POINTER, new->pseudo, expr->label_symbol);
	return new;
}

static struct storage *x86_statement_expr(struct expression *expr)
{
	return x86_statement(expr->statement);
}

static int x86_position_expr(struct expression *expr, struct symbol *base)
{
	struct storage *new = x86_expression(expr->init_expr);
	struct symbol *ctype = expr->init_sym;

	printf("\tinsert v%d at [%d:%d] of %s\n", new->pseudo,
		expr->init_offset, ctype->bit_offset,
		show_ident(base->ident));
	return 0;
}

static void x86_initializer_expr(struct expression *expr, struct symbol *ctype)
{
	struct expression *entry;

	FOR_EACH_PTR(expr->expr_list, entry) {
		// Nested initializers have their positions already
		// recursively calculated - just output them too
		if (entry->type == EXPR_INITIALIZER) {
			x86_initializer_expr(entry, ctype);
			continue;
		}

		// Ignore initializer indexes and identifiers - the
		// evaluator has taken them into account
		if (entry->type == EXPR_IDENTIFIER || entry->type == EXPR_INDEX)
			continue;
		if (entry->type == EXPR_POS) {
			x86_position_expr(entry, ctype);
			continue;
		}
		x86_initialization(ctype, entry);
	} END_FOR_EACH_PTR;
}

/*
 * Print out an expression. Return the pseudo that contains the
 * variable.
 */
static struct storage *x86_expression(struct expression *expr)
{
	if (!expr)
		return 0;

	if (!expr->ctype) {
		struct position *pos = &expr->pos;
		printf("\tno type at %s:%d:%d\n",
			input_streams[pos->stream].name,
			pos->line, pos->pos);
		return 0;
	}

	switch (expr->type) {
	case EXPR_CALL:
		return x86_call_expression(expr);

	case EXPR_ASSIGNMENT:
		return x86_assignment(expr);

	case EXPR_COMPARE:
		return emit_compare(expr);
	case EXPR_BINOP:
	case EXPR_COMMA:
	case EXPR_LOGICAL:
		return emit_binop(expr);
	case EXPR_PREOP:
		return x86_preop(expr);
	case EXPR_POSTOP:
		return emit_postop(expr);
	case EXPR_SYMBOL:
		return emit_symbol_expr_init(expr->symbol);
	case EXPR_DEREF:
	case EXPR_SIZEOF:
		warn(expr->pos, "invalid expression after evaluation");
		return 0;
	case EXPR_CAST:
		return emit_cast_expr(expr);
	case EXPR_VALUE:
		return emit_value(expr);
	case EXPR_STRING:
		return emit_string_expr(expr);
	case EXPR_BITFIELD:
		return x86_bitfield_expr(expr);
	case EXPR_INITIALIZER:
		x86_initializer_expr(expr, expr->ctype);
		return NULL;
	case EXPR_CONDITIONAL:
		return emit_conditional_expr(expr);
	case EXPR_STATEMENT:
		return x86_statement_expr(expr);
	case EXPR_LABEL:
		return x86_label_expr(expr);

	// None of these should exist as direct expressions: they are only
	// valid as sub-expressions of initializers.
	case EXPR_POS:
		warn(expr->pos, "unable to show plain initializer position expression");
		return 0;
	case EXPR_IDENTIFIER:
		warn(expr->pos, "unable to show identifier expression");
		return 0;
	case EXPR_INDEX:
		warn(expr->pos, "unable to show index expression");
		return 0;
	}
	return 0;
}
