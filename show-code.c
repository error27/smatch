/* First-draft C printer for Sparse ASTs. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expression.h"
#include "parse.h"
#include "scope.h"
#include "symbol.h"
#include "token.h"

struct code_buffer {
	char *buf;
	size_t len, used;
	unsigned int indent;
	bool line_start;
	struct symbol *local_tags[1024];
	unsigned int local_tag_count;
};

static void append(struct code_buffer *out, const char *fmt, ...)
{
	char stack_buffer[4096];
	char *buffer = stack_buffer;
	va_list ap;
	int ret;
	int i;

	if (out->used >= out->len)
		return;
	va_start(ap, fmt);
	ret = vsnprintf(buffer, sizeof(stack_buffer), fmt, ap);
	va_end(ap);
	if (ret <= 0)
		return;
	if (ret >= sizeof(stack_buffer)) {
		buffer = malloc(ret + 1);
		if (!buffer)
			return;
		va_start(ap, fmt);
		vsnprintf(buffer, ret + 1, fmt, ap);
		va_end(ap);
	}
	for (i = 0; i < ret; i++) {
		unsigned int tab;

		if (out->line_start && buffer[i] != '\n') {
			for (tab = 0; tab < out->indent && out->used + 1 < out->len; tab++)
				out->buf[out->used++] = '\t';
			out->line_start = false;
		}
		if (out->used + 1 >= out->len)
			break;
		out->buf[out->used++] = buffer[i];
		out->line_start = buffer[i] == '\n';
	}
	out->buf[out->used] = '\0';
	if (buffer != stack_buffer)
		free(buffer);
}

static void expr(struct code_buffer *out, struct expression *e);
static void stmt(struct code_buffer *out, struct statement *s);
static void show_statement_expression(struct code_buffer *out,
				      struct statement *s);
static void sym(struct code_buffer *out, struct symbol *s);
static void show_function(struct code_buffer *out, struct symbol *s);
static struct symbol *find_anonymous_tag(struct symbol *type);
static void show_anonymous_c_typename(struct code_buffer *out,
				      struct symbol *type,
				      struct symbol *anonymous_tag);
static void show_initializer_dependencies(struct code_buffer *out,
					  struct symbol *symbol);
static bool is_type_definition(struct symbol *s, struct symbol **tag_out);
static bool tag_has_definition(struct symbol *tag);
static void show_local_tag_definition(struct code_buffer *out,
				      struct symbol *tag);
static bool output_whole_file;

void set_show_code_whole_file(bool whole_file)
{
	output_whole_file = whole_file;
}
static bool is_struct_or_union(struct symbol *type);
static const char *tag_type_name(struct symbol *type);
static void collect_statement_functions(struct symbol **functions,
					unsigned int *count, struct statement *s);
static void collect_type_expression_functions(struct symbol **functions,
					      unsigned int *count,
					      struct symbol *type);

struct struct_types {
	struct symbol *types[8192];
	bool needs_forward[8192];
	unsigned int count;
};

static struct symbol *find_type_expression(struct symbol *type)
{
	while (type) {
		if (type->type_expression)
			return type;
		type = type->ctype.base_type;
	}
	return NULL;
}

static struct symbol *base_type_for_output(struct symbol *type,
					   struct symbol *copies,
					   unsigned int *count)
{
	struct symbol *base_type;
	struct symbol *copy;

	if (!type || type->type == SYM_BASETYPE)
		return type;
	if (type->type_expression) {
		if (*count >= 64)
			return type;
		copy = &copies[(*count)++];
		*copy = *type;
		copy->type = SYM_STRUCT;
		copy->ident = built_in_ident("__extract_typeof_placeholder");
		copy->ctype.base_type = NULL;
		return copy;
	}
	base_type = base_type_for_output(type->ctype.base_type, copies, count);
	if (type->type != SYM_RESTRICT && base_type == type->ctype.base_type)
		return type;
	if (*count >= 64)
		return type;
	copy = &copies[(*count)++];
	*copy = *type;
	copy->ctype.base_type = base_type;
	if (copy->type == SYM_RESTRICT)
		copy->ident = NULL;
	return copy;
}

static void show_type_expression_storage(struct code_buffer *out,
					 struct symbol *s)
{
	if (s->ctype.modifiers & MOD_STATIC)
		append(out, "static ");
	else if ((s->ctype.modifiers & MOD_EXTERN) &&
		 !(s->ctype.modifiers & MOD_REGISTER))
		append(out, "extern ");
	if (s->ctype.modifiers & MOD_REGISTER)
		append(out, "register ");
}

static bool show_type_expression_typename(struct code_buffer *out, struct symbol *s)
{
	struct symbol *type;

	if (!s)
		return false;
	if (s->type == SYM_NODE && s->ctype.base_type == &autotype_ctype) {
		append(out, "__auto_type");
		if (s->ident)
			append(out, " %s", show_ident(s->ident));
		return true;
	}
	if (s->type == SYM_NODE)
		type = s->ctype.base_type;
	else
		type = s;
	if (type && type->type_expression) {
		show_type_expression_storage(out, s);
		append(out, "%s(", type->typeof_is_unqual ? "__typeof_unqual__" :
		       "typeof");
		expr(out, type->type_expression);
		append(out, ")");
		if (s->ident)
			append(out, " %s", show_ident(s->ident));
		return true;
	}
	if (!type || (type->type != SYM_TYPEOF &&
		      type->type != SYM_TYPEOF_UNQUAL))
		return false;
	show_type_expression_storage(out, s);
	append(out, "%s(", type->type == SYM_TYPEOF ? "typeof" :
	       "__typeof_unqual__");
	expr(out, type->initializer);
	append(out, ")");
	if (s->ident)
		append(out, " %s", show_ident(s->ident));
	return true;
}

static void show_c_typename(struct code_buffer *out, struct symbol *s)
{
	struct symbol copies[64];
	struct symbol copy;
	struct symbol *type_expression;
	char typename[8192];
	char *placeholder;
	unsigned int count = 0;

	if (show_type_expression_typename(out, s))
		return;
	if (s->ctype.modifiers & MOD_REGISTER) {
		copy = *s;
		copy.ctype.modifiers &= ~MOD_EXTERN;
		s = &copy;
	}
	type_expression = find_type_expression(s);
	snprintf(typename, sizeof(typename), "%s",
		 show_typename(base_type_for_output(s, copies, &count), false));
	if (!type_expression ||
	    !(placeholder = strstr(typename,
				    "struct __extract_typeof_placeholder"))) {
		append(out, "%s", typename);
		return;
	}
	*placeholder = '\0';
	append(out, "%s%s(", typename,
	       type_expression->typeof_is_unqual ? "__typeof_unqual__" :
	       "typeof");
	expr(out, type_expression->type_expression);
	append(out, ")%s", placeholder + strlen("struct __extract_typeof_placeholder"));
}

static void show_asm_name(struct code_buffer *out, struct symbol *s)
{
	if (!s->asm_name)
		return;
	append(out, " asm(");
	expr(out, s->asm_name);
	append(out, ")");
}

static void exprs(struct code_buffer *out, struct expression_list *list)
{
	struct expression *e;
	bool comma = false;

	FOR_EACH_PTR(list, e) {
		if (comma)
			append(out, ", ");
		expr(out, e);
		comma = true;
	} END_FOR_EACH_PTR(e);
}

static void show_literal_type(struct code_buffer *out, struct symbol *type)
{
	char buf[1024];
	struct code_buffer type_out = {
		.buf = buf,
		.len = sizeof(buf),
		.line_start = true,
	};

	buf[0] = '\0';
	show_c_typename(&type_out, type);
	append(out, "%s", buf);
}

static void show_value(struct code_buffer *out, struct expression *e)
{
	if (!e->ctype) {
		append(out, "0x%llx", e->value);
		return;
	}
	append(out, "((");
	show_literal_type(out, e->ctype);
	append(out, ")0x%llx)", e->value);
}

static void show_fvalue(struct code_buffer *out, struct expression *e)
{
	if (!e->ctype) {
		append(out, "%.36Lg", e->fvalue);
		return;
	}
	append(out, "((");
	show_literal_type(out, e->ctype);
	append(out, ")%.36Lg)", e->fvalue);
}

static void show_type_expression(struct code_buffer *out, struct expression *e)
{
	struct symbol *type = e->symbol ? e->symbol->ctype.base_type : NULL;

	if (e->symbol && e->symbol->type_expression) {
		append(out, "%s(", e->symbol->typeof_is_unqual ?
		       "__typeof_unqual__" : "typeof");
		expr(out, e->symbol->type_expression);
		append(out, ")");
		return;
	}
	if (type && (type->type == SYM_TYPEOF ||
		     type->type == SYM_TYPEOF_UNQUAL)) {
		append(out, type->type == SYM_TYPEOF_UNQUAL ? "__typeof_unqual__(" : "typeof(");
		expr(out, type->initializer);
		append(out, ")");
		return;
	}
	show_c_typename(out, e->symbol);
}

static void show_type_info_expression(struct code_buffer *out,
				      struct expression *e, const char *name)
{
	append(out, "%s(", name);
	if (e->cast_type) {
		struct symbol *anonymous_tag = find_anonymous_tag(e->cast_type);

		if (anonymous_tag)
			show_anonymous_c_typename(out, e->cast_type, anonymous_tag);
		else
			show_c_typename(out, e->cast_type);
	} else {
		expr(out, e->cast_expression);
	}
	append(out, ")");
}

static void show_offsetof_expression(struct code_buffer *out,
				   struct expression *e)
{
	struct expression *part;
	bool dereference = false;
	bool first = true;

	append(out, "__builtin_offsetof(");
	show_c_typename(out, e->in);
	append(out, ", ");
	for (part = e; part; part = part->down) {
		if (part->op == '[' && !part->index) {
			dereference = true;
			continue;
		}
		if (part->op == '.')
			append(out, "%s%s", first ? "" : dereference ? "->" : ".",
			       show_ident(part->ident));
		else {
			append(out, "[");
			expr(out, part->index);
			append(out, "]");
		}
		dereference = false;
		first = false;
	}
	append(out, ")");
}

static void show_generic_expression(struct code_buffer *out, struct expression *e)
{
	struct type_expression *map;

	append(out, "_Generic(");
	expr(out, e->control);
	for (map = e->map; map; map = map->next) {
		append(out, ", ");
		show_c_typename(out, map->type);
		append(out, ": ");
		expr(out, map->expr);
	}
	if (e->def) {
		append(out, ", default: ");
		expr(out, e->def);
	}
	append(out, ")");
}

static bool is_implicit_dereference(struct expression *e)
{
	struct expression *unop;

	if (!e || e->type != EXPR_PREOP || e->op != '*')
		return false;
	unop = e->unop;
	return unop && unop->ctype == &lazy_ptr_ctype &&
		e->pos.stream == unop->pos.stream &&
		e->pos.line == unop->pos.line &&
		e->pos.pos == unop->pos.pos;
}

static void show_initializer_designator(struct code_buffer *out,
					struct expression *e)
{
	if (e->type == EXPR_IDENTIFIER) {
		append(out, ".%s", show_ident(e->expr_ident));
		e = e->ident_expression;
	} else {
		append(out, "[%u", e->idx_from);
		if (e->idx_from != e->idx_to)
			append(out, " ... %u", e->idx_to);
		append(out, "]");
		e = e->idx_expression;
	}
	if (e && (e->type == EXPR_IDENTIFIER || e->type == EXPR_INDEX))
		show_initializer_designator(out, e);
	else {
		append(out, " = ");
		expr(out, e);
	}
}

static void expr(struct code_buffer *out, struct expression *e)
{
	if (!e) { append(out, "/* null */"); return; }
	switch (e->type) {
	case EXPR_VALUE: show_value(out, e); break;
	case EXPR_FVALUE: show_fvalue(out, e); break;
	case EXPR_STRING: append(out, "%s", show_string(e->string)); break;
	case EXPR_TYPE: show_type_expression(out, e); break;
	case EXPR_SYMBOL:
		if (e->symbol && e->symbol->ident)
			append(out, "%s", show_ident(e->symbol->ident));
		else if (e->symbol_name)
			append(out, "%s", show_ident(e->symbol_name));
		else
			append(out, "/* anonymous symbol */");
		break;
	case EXPR_PREOP:
		if (e->op == '&' && e->unop && e->unop->type == EXPR_DEREF &&
		    e->unop->member && e->unop->deref &&
		    e->unop->deref->type == EXPR_PREOP &&
		    e->unop->deref->op == '*') {
			append(out, "&");
			expr(out, e->unop->deref->unop);
			append(out, "->%s", show_ident(e->unop->member));
		} else if (is_implicit_dereference(e)) {
			expr(out, e->unop);
		} else if (e->op == '(') {
			append(out, "(");
			expr(out, e->unop);
			append(out, ")");
		} else {
			append(out, "(%s", show_special(e->op));
			expr(out, e->unop);
			append(out, ")");
		}
		break;
	case EXPR_POSTOP: append(out, "("); expr(out, e->unop); append(out, "%s)", show_special(e->op)); break;
	case EXPR_COMPARE:
		if (e->left && e->left->type == EXPR_TYPE &&
		    e->right && e->right->type == EXPR_TYPE) {
			append(out, "__builtin_types_compatible_p(");
			show_type_expression(out, e->left);
			append(out, ", ");
			show_type_expression(out, e->right);
			append(out, ")");
			break;
		}
		/* Fall through */
	case EXPR_BINOP: case EXPR_LOGICAL:
	case EXPR_ASSIGNMENT: case EXPR_COMMA:
		append(out, "("); expr(out, e->left); append(out, " %s ", show_special(e->op)); expr(out, e->right); append(out, ")"); break;
	case EXPR_DEREF:
		append(out, "(");
		if (e->member && e->deref && e->deref->type == EXPR_PREOP &&
		    e->deref->op == '*') {
			expr(out, e->deref->unop);
			append(out, "->%s", show_ident(e->member));
		} else {
			expr(out, e->deref);
			if (e->member)
				append(out, ".%s", show_ident(e->member));
		}
		append(out, ")");
		break;
	case EXPR_CALL:
		if (e->fn && e->fn->type == EXPR_PREOP && e->fn->op == '*') {
			struct expression *index = e->fn->unop;

			if (index && index->type == EXPR_BINOP && index->op == '+') {
				expr(out, index->left);
				append(out, "[");
				expr(out, index->right);
				append(out, "]");
			} else {
				expr(out, e->fn->unop);
			}
		} else {
			expr(out, e->fn);
		}
		append(out, "("); exprs(out, e->args); append(out, ")"); break;
	case EXPR_CAST: case EXPR_FORCE_CAST:
		append(out, "(("); show_c_typename(out, e->cast_type); append(out, ")"); expr(out, e->cast_expression); append(out, ")"); break;
	case EXPR_IMPLIED_CAST:
		expr(out, e->cast_expression);
		break;
	case EXPR_SIZEOF:
		show_type_info_expression(out, e, "sizeof");
		break;
	case EXPR_ALIGNOF:
		show_type_info_expression(out, e, "__alignof__");
		break;
	case EXPR_PTRSIZEOF:
		show_type_info_expression(out, e, "__sizeof_ptr__");
		break;
	case EXPR_STATEMENT:
		show_statement_expression(out, e->statement);
		break;
	case EXPR_CONDITIONAL: case EXPR_SELECT:
		append(out, "("); expr(out, e->conditional); append(out, " ? "); expr(out, e->cond_true); append(out, " : "); expr(out, e->cond_false); append(out, ")"); break;
	case EXPR_LABEL:
		append(out, "&&%s", show_ident(e->label_symbol->ident));
		break;
	case EXPR_INITIALIZER:
		append(out, "{");
		if (e->expr_list) {
			append(out, " ");
			exprs(out, e->expr_list);
			append(out, " ");
		}
		append(out, "}");
		break;
	case EXPR_IDENTIFIER:
	case EXPR_INDEX:
		show_initializer_designator(out, e);
		break;
	case EXPR_POS:
		expr(out, e->init_expr);
		break;
	case EXPR_SLICE:
		expr(out, e->base);
		break;
	case EXPR_OFFSETOF:
		show_offsetof_expression(out, e);
		break;
	case EXPR_GENERIC:
		show_generic_expression(out, e);
		break;
	default: append(out, "/* unsupported expression %d */", e->type); break;
	}
}

static void show_anonymous_tag(struct code_buffer *out, struct symbol *type);

static void declaration(struct code_buffer *out, struct symbol *s)
{
	struct symbol *anonymous_tag = find_anonymous_tag(s);
	struct symbol *tag = s;
	unsigned int i;

	while (tag) {
		if ((is_struct_or_union(tag) || tag->type == SYM_ENUM) &&
		    tag->ident && tag_has_definition(tag) && tag->scope &&
		    !toplevel(tag->scope))
			break;
		tag = tag->ctype.base_type;
	}
	for (i = 0; tag && i < out->local_tag_count; i++) {
		if (out->local_tags[i] == tag)
			break;
	}
	if (tag && i == out->local_tag_count) {
		show_local_tag_definition(out, tag);
		if (out->local_tag_count < ARRAY_SIZE(out->local_tags))
			out->local_tags[out->local_tag_count++] = tag;
	}

	if (anonymous_tag && s->initializer && !find_type_expression(s)) {
		show_type_expression_storage(out, s);
		append(out, "typeof(");
		expr(out, s->initializer);
		append(out, ")");
		if (s->ident)
			append(out, " %s", show_ident(s->ident));
	} else if (find_type_expression(s)) {
		show_c_typename(out, s);
	} else if (anonymous_tag) {
		show_anonymous_c_typename(out, s, anonymous_tag);
	} else {
		show_c_typename(out, s);
	}
	show_asm_name(out, s);
	if (s->initializer) {
		append(out, " = ");
		expr(out, s->initializer);
	}
	append(out, ";\n");
}

static void declarations(struct code_buffer *out, struct symbol_list *list)
{
	struct symbol *s;

	FOR_EACH_PTR(list, s) {
		declaration(out, s);
	} END_FOR_EACH_PTR(s);
}

static void iterator_statement(struct code_buffer *out, struct statement *s)
{
	if (!s || s->type == STMT_NONE)
		return;
	if (s->type == STMT_EXPRESSION) {
		expr(out, s->expression);
		return;
	}
	append(out, "/* unsupported iterator statement %d */", s->type);
}

static void asm_string(struct code_buffer *out, struct expression *e)
{
	if (e && e->type == EXPR_STRING)
		append(out, "%s", show_string(e->string));
	else
		expr(out, e);
}

static bool is_constant_asm_expression(struct expression *e)
{
	if (!e)
		return false;
	switch (e->type) {
	case EXPR_VALUE:
	case EXPR_FVALUE:
		return true;
	case EXPR_PREOP:
	case EXPR_POSTOP:
		return is_constant_asm_expression(e->unop);
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
		return is_constant_asm_expression(e->cast_expression);
	case EXPR_BINOP:
	case EXPR_LOGICAL:
	case EXPR_COMPARE:
		return is_constant_asm_expression(e->left) &&
		       is_constant_asm_expression(e->right);
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		return is_constant_asm_expression(e->conditional) &&
		       is_constant_asm_expression(e->cond_true) &&
		       is_constant_asm_expression(e->cond_false);
	default:
		return false;
	}
}

static bool inline_asm_immediate(struct asm_operand *op)
{
	struct expression *e = op->expr;

	return op->constraint && op->constraint->type == EXPR_STRING &&
	       op->constraint->string->length == 2 &&
	       op->constraint->string->data[0] == 'i' &&
	       op->constraint->string->data[1] == '\0' &&
	       e && e->type == EXPR_SYMBOL && e->symbol &&
	       e->symbol->initializer &&
	       is_constant_asm_expression(e->symbol->initializer);
}

static void asm_operands(struct code_buffer *out, struct asm_operand_list *list)
{
	struct asm_operand *op;
	bool comma = false;

	FOR_EACH_PTR(list, op) {
		if (comma)
			append(out, ", ");
		if (op->name)
			append(out, "[%s] ", show_ident(op->name));
		asm_string(out, op->constraint);
		append(out, " (");
		if (inline_asm_immediate(op))
			expr(out, op->expr->symbol->initializer);
		else
			expr(out, op->expr);
		append(out, ")");
		comma = true;
	} END_FOR_EACH_PTR(op);
}

static void asm_clobbers(struct code_buffer *out, struct expression_list *list)
{
	struct expression *e;
	bool comma = false;

	FOR_EACH_PTR(list, e) {
		if (comma)
			append(out, ", ");
		asm_string(out, e);
		comma = true;
	} END_FOR_EACH_PTR(e);
}

static void asm_labels(struct code_buffer *out, struct symbol_list *list)
{
	struct symbol *s;
	bool comma = false;

	FOR_EACH_PTR(list, s) {
		if (comma)
			append(out, ", ");
		append(out, "%s", show_ident(s->ident));
		comma = true;
	} END_FOR_EACH_PTR(s);
}

static void show_local_labels(struct code_buffer *out,
			      struct symbol_list *labels)
{
	struct symbol *label;
	bool comma = false;

	if (!labels)
		return;
	append(out, "__label__ ");
	FOR_EACH_PTR(labels, label) {
		if (comma)
			append(out, ", ");
		append(out, "%s", show_ident(label->ident));
		comma = true;
	} END_FOR_EACH_PTR(label);
	append(out, ";\n");
}

static void stmt(struct code_buffer *out, struct statement *s)
{
	struct statement *child;
	if (!s) { append(out, "{}\n"); return; }
	switch (s->type) {
	case STMT_NONE:
		append(out, ";\n");
		break;
	case STMT_COMPOUND:
		append(out, "{\n");
		out->indent++;
		show_local_labels(out, s->local_labels);
		FOR_EACH_PTR(s->stmts, child) {
			stmt(out, child);
		} END_FOR_EACH_PTR(child);
		out->indent--;
		append(out, "}\n");
		break;
	case STMT_DECLARATION: declarations(out, s->declaration); break;
	case STMT_EXPRESSION: expr(out, s->expression); append(out, ";\n"); break;
	case STMT_RETURN: append(out, "return"); if (s->ret_value) { append(out, " "); expr(out, s->ret_value); } append(out, ";\n"); break;
	case STMT_LABEL:
		append(out, "%s:\n", show_ident(s->label_identifier->ident));
		if (s->label_statement && s->label_statement->type != STMT_NONE)
			stmt(out, s->label_statement);
		else
			append(out, ";\n");
		break;
	case STMT_GOTO:
		if (s->goto_expression) {
			append(out, "goto *");
			expr(out, s->goto_expression);
			append(out, ";\n");
		} else if (s->goto_label && s->goto_label->ident == &break_ident) {
			append(out, "break;\n");
		} else if (s->goto_label && s->goto_label->ident == &continue_ident) {
			append(out, "continue;\n");
		} else if (s->goto_label && s->goto_label->ident) {
			append(out, "goto %s;\n", show_ident(s->goto_label->ident));
		} else {
			append(out, "/* invalid goto */\n");
		}
		break;
	case STMT_ASM:
		append(out, "asm");
		if (s->asm_modifiers & MOD_INLINE)
			append(out, " inline");
		if (s->asm_modifiers & MOD_VOLATILE)
			append(out, " volatile");
		if (s->asm_modifiers & MOD_ASM_GOTO)
			append(out, " goto");
		append(out, " (");
		asm_string(out, s->asm_string);
		if (s->asm_outputs || s->asm_inputs || s->asm_clobbers ||
		    s->asm_labels) {
			append(out, " : ");
			asm_operands(out, s->asm_outputs);
		}
		if (s->asm_inputs || s->asm_clobbers || s->asm_labels) {
			append(out, " : ");
			asm_operands(out, s->asm_inputs);
		}
		if (s->asm_clobbers || s->asm_labels) {
			append(out, " : ");
			asm_clobbers(out, s->asm_clobbers);
		}
		if (s->asm_labels) {
			append(out, " : ");
			asm_labels(out, s->asm_labels);
		}
		append(out, ");\n");
		break;
	case STMT_CONTEXT:
		append(out, "/* __context__(");
		if (s->context) {
			expr(out, s->context);
			append(out, ", ");
		}
		expr(out, s->expression);
		append(out, ") */;\n");
		break;
	case STMT_RANGE:
		append(out, "/* __range__(");
		expr(out, s->range_expression);
		append(out, ", ");
		expr(out, s->range_low);
		append(out, ", ");
		expr(out, s->range_high);
		append(out, ") */;\n");
		break;
	case STMT_SWITCH:
		append(out, "switch (");
		expr(out, s->switch_expression);
		append(out, ") ");
		stmt(out, s->switch_statement);
		break;
	case STMT_CASE:
		if (s->case_expression) {
			append(out, "case ");
			expr(out, s->case_expression);
			if (s->case_to) {
				append(out, " ... ");
				expr(out, s->case_to);
			}
			append(out, ":\n");
		} else {
			append(out, "default:\n");
		}
		stmt(out, s->case_statement);
		break;
	case STMT_IF:
		append(out, "if ("); expr(out, s->if_conditional); append(out, ") "); stmt(out, s->if_true);
		if (s->if_false) { append(out, "else "); stmt(out, s->if_false); } break;
	case STMT_ITERATOR:
		if (s->iterator_syms || s->iterator_pre_statement ||
		    s->iterator_post_statement) {
			if (s->iterator_syms) {
				append(out, "{\n");
				out->indent++;
				declarations(out, s->iterator_syms);
			}
			append(out, "for (");
			if (!s->iterator_syms)
				iterator_statement(out, s->iterator_pre_statement);
			append(out, "; ");
			if (s->iterator_pre_condition)
				expr(out, s->iterator_pre_condition);
			append(out, "; ");
			iterator_statement(out, s->iterator_post_statement);
			append(out, ") ");
			stmt(out, s->iterator_statement);
			if (s->iterator_syms) {
				out->indent--;
				append(out, "}\n");
			}
		} else if (s->iterator_pre_condition) {
			append(out, "while (");
			expr(out, s->iterator_pre_condition);
			append(out, ") ");
			stmt(out, s->iterator_statement);
		} else if (s->iterator_post_condition) {
			append(out, "do ");
			stmt(out, s->iterator_statement);
			if (s->iterator_statement &&
			    s->iterator_statement->type == STMT_COMPOUND &&
			    out->used && out->buf[out->used - 1] == '\n') {
				out->buf[--out->used] = '\0';
				out->line_start = false;
			}
			append(out, " while (");
			expr(out, s->iterator_post_condition);
			append(out, ");\n");
		} else {
			append(out, "for(;;) ");
			stmt(out, s->iterator_statement);
		}
		break;
	default: append(out, "/* unsupported statement %d */\n", s->type); break;
	}
}

static void show_statement_expression(struct code_buffer *out,
				      struct statement *s)
{
	struct statement *child;

	if (!s || s->type != STMT_COMPOUND) {
		append(out, "(");
		stmt(out, s);
		append(out, ")");
		return;
	}
	append(out, "({\n");
	out->indent++;
	show_local_labels(out, s->local_labels);
	FOR_EACH_PTR(s->stmts, child) {
		stmt(out, child);
	} END_FOR_EACH_PTR(child);
	out->indent--;
	append(out, "})");
}

static void function_body(struct code_buffer *out, struct statement *s)
{
	struct statement *child;
	bool initial_declarations = true;

	if (!s || s->type != STMT_COMPOUND) {
		stmt(out, s);
		return;
	}
	append(out, "{\n");
	out->indent++;
	show_local_labels(out, s->local_labels);
	FOR_EACH_PTR(s->stmts, child) {
		if (initial_declarations && child->type != STMT_DECLARATION) {
			append(out, "\n");
			initial_declarations = false;
		}
		stmt(out, child);
	} END_FOR_EACH_PTR(child);
	out->indent--;
	append(out, "}\n");
}

static bool tag_has_definition(struct symbol *tag)
{
	return tag && (is_struct_or_union(tag) || tag->type == SYM_ENUM) &&
		tag->endpos.type;
}

static bool is_type_definition(struct symbol *s, struct symbol **tag_out)
{
	struct symbol *tag;

	for (tag = s; tag; tag = tag->ctype.base_type) {
		if (tag->type == SYM_FN)
			return false;
		if (is_struct_or_union(tag) || tag->type == SYM_ENUM)
			break;
	}
	if (!tag_has_definition(tag) ||
	    (s != tag && s->definition != tag && s->namespace != NS_TYPEDEF))
		return false;
	*tag_out = tag;
	return true;
}

static void sym(struct code_buffer *out, struct symbol *s)
{
	struct symbol *type;

	if (!s) {
		append(out, "/* null symbol */");
		return;
	}

	type = s->ctype.base_type;

	if (type && type->type == SYM_FN) {
		show_function(out, s);
		return;
	}

	if (is_type_definition(s, &type)) {
		return;
	}
	show_initializer_dependencies(out, s);
	declaration(out, s);
}

static bool is_struct_or_union(struct symbol *type)
{
	return type && (type->type == SYM_STRUCT || type->type == SYM_UNION);
}

static const char *tag_type_name(struct symbol *type)
{
	if (type->type == SYM_ENUM)
		return "enum";
	return type->type == SYM_UNION ? "union" : "struct";
}

static bool is_typedef_tag(struct symbol *type)
{
	struct symbol *typedef_symbol;

	if (type->namespace == NS_TYPEDEF)
		return true;
	if (!type->ident)
		return false;
	typedef_symbol = lookup_symbol(type->ident, NS_TYPEDEF);
	while (typedef_symbol) {
		if (typedef_symbol == type || typedef_symbol->definition == type)
			return true;
		typedef_symbol = typedef_symbol->ctype.base_type;
	}
	return false;
}

static void collect_struct_types(struct struct_types *types,
				 struct symbol *struct_type);

static struct symbol *strip_type_wrappers(struct symbol *type)
{
	while (type && (type->type == SYM_NODE || type->type == SYM_RESTRICT))
		type = get_base_type(type);
	return type;
}

static struct symbol *strip_bitfield_type(struct symbol *type)
{
	type = strip_type_wrappers(type);
	while (type && type->type == SYM_BITFIELD)
		type = strip_type_wrappers(get_base_type(type));
	return type;
}

static bool type_is_ptr(struct symbol *type)
{
	return type && type->type == SYM_PTR;
}

static bool type_is_array(struct symbol *type)
{
	return type && type->type == SYM_ARRAY;
}

static bool type_needs_forward(struct symbol *type)
{
	while (type_is_array(type))
		type = strip_type_wrappers(get_base_type(type));
	if (!type_is_ptr(type))
		return false;
	type = strip_type_wrappers(get_base_type(type));
	return !type || type->type != SYM_ENUM;
}

static struct symbol *add_struct_type(struct struct_types *types,
					      struct symbol *type, bool needs_forward)
{
	unsigned int i;

	type = strip_type_wrappers(type);
	while (type_is_ptr(type) || type_is_array(type))
		type = strip_type_wrappers(get_base_type(type));
	if (type && type->definition &&
	    (is_struct_or_union(type->definition) ||
	     type->definition->type == SYM_ENUM))
		type = type->definition;
	if (!type ||
	    (!is_struct_or_union(type) && type->type != SYM_ENUM) || !type->ident)
		return NULL;
	for (i = 0; i < types->count; i++) {
		if (types->types[i] != type)
			continue;
		types->needs_forward[i] &= needs_forward;
		return type;
	}
	if (types->count >= ARRAY_SIZE(types->types))
		return NULL;
	types->types[types->count] = type;
	types->needs_forward[types->count++] = needs_forward;
	return type;
}

static void handle_functions(struct struct_types *types, struct symbol *type)
{
	struct symbol *arg;

	while (type_is_ptr(type) || type_is_array(type))
		type = strip_type_wrappers(get_base_type(type));
	if (!type || type->type != SYM_FN)
		return;
	FOR_EACH_PTR(type->arguments, arg) {
		struct symbol *arg_type = strip_type_wrappers(arg);
		struct symbol *struct_type = add_struct_type(types, arg_type,
							     type_needs_forward(arg_type));

		if (struct_type && !type_needs_forward(arg_type))
			collect_struct_types(types, struct_type);
	} END_FOR_EACH_PTR(arg);
}

static void collect_struct_types(struct struct_types *types,
				 struct symbol *struct_type)
{
	struct symbol *member;

	if (!is_struct_or_union(struct_type) || !struct_type->symbol_list)
		return;
	FOR_EACH_PTR(struct_type->symbol_list, member) {
		struct symbol *member_type = strip_bitfield_type(member);
		struct symbol *nested_type = member_type;
		struct symbol *struct_type = add_struct_type(types, member_type,
							     type_needs_forward(member_type));

		while (type_is_array(nested_type))
			nested_type = strip_type_wrappers(get_base_type(nested_type));
		if (!struct_type &&
		    is_struct_or_union(nested_type))
			collect_struct_types(types, nested_type);
		if (struct_type && !type_needs_forward(member_type))
			collect_struct_types(types, struct_type);
		if (type_is_ptr(member_type) || type_is_array(member_type))
			handle_functions(types, member_type);
	} END_FOR_EACH_PTR(member);
}

static bool symbol_defines_tag(struct symbol *symbol, struct symbol *tag)
{
	while (symbol) {
		if (symbol == tag || symbol->definition == tag)
			return true;
		symbol = symbol->ctype.base_type;
	}
	return false;
}

static struct symbol *find_anonymous_tag(struct symbol *type)
{
	while (type) {
		if ((is_struct_or_union(type) || type->type == SYM_ENUM) &&
		    !type->ident)
			return type;
		type = type->ctype.base_type;
	}
	return NULL;
}

static void show_anonymous_tag(struct code_buffer *out, struct symbol *type)
{
	struct symbol *member;

	if (type->type == SYM_ENUM) {
		append(out, "enum {\n");
		out->indent++;
		FOR_EACH_PTR(type->symbol_list, member) {
			append(out, "%s", show_ident(member->ident));
			if (member->initializer) {
				append(out, " = ");
				expr(out, member->initializer);
			}
			append(out, ",\n");
		} END_FOR_EACH_PTR(member);
		out->indent--;
		append(out, "}");
		return;
	}
	append(out, "%s {\n", tag_type_name(type));
	out->indent++;
	FOR_EACH_PTR(type->symbol_list, member) {
		struct symbol *anonymous_tag = find_anonymous_tag(member);

		if (anonymous_tag) {
			show_anonymous_c_typename(out, member, anonymous_tag);
			append(out, ";\n");
			continue;
		}
		show_c_typename(out, member);
		append(out, ";\n");
	} END_FOR_EACH_PTR(member);
	out->indent--;
	append(out, "}");
}

static void show_anonymous_c_typename(struct code_buffer *out,
				      struct symbol *type,
				      struct symbol *anonymous_tag)
{
	char buf[4096];
	char placeholder[64];
	struct code_buffer type_out = {
		.buf = buf,
		.len = sizeof(buf),
		.line_start = true,
	};
	char *tag;

	buf[0] = '\0';
	show_c_typename(&type_out, type);
	snprintf(placeholder, sizeof(placeholder), "%s <noident>",
		 tag_type_name(anonymous_tag));
	tag = strstr(buf, placeholder);
	if (!tag) {
		show_anonymous_tag(out, anonymous_tag);
		if (type->ident)
			append(out, " %s", show_ident(type->ident));
		return;
	}
	*tag = '\0';
	append(out, "%s", buf);
	show_anonymous_tag(out, anonymous_tag);
	append(out, "%s", tag + strlen(placeholder));
}

static void show_local_tag_definition(struct code_buffer *out,
				      struct symbol *tag)
{
	struct symbol *member;

	if (tag->type == SYM_ENUM) {
		append(out, "enum %s {\n", show_ident(tag->ident));
		out->indent++;
		FOR_EACH_PTR(tag->symbol_list, member) {
			append(out, "%s", show_ident(member->ident));
			if (member->initializer) {
				append(out, " = ");
				expr(out, member->initializer);
			}
			append(out, ",\n");
		} END_FOR_EACH_PTR(member);
		out->indent--;
		append(out, "};\n");
		return;
	}

	append(out, "%s %s {\n", tag_type_name(tag), show_ident(tag->ident));
	out->indent++;
	FOR_EACH_PTR(tag->symbol_list, member) {
		struct symbol *anonymous_tag = find_anonymous_tag(member);

		if (anonymous_tag) {
			show_anonymous_c_typename(out, member, anonymous_tag);
			append(out, ";\n");
			continue;
		}
		show_c_typename(out, member);
		append(out, ";\n");
	} END_FOR_EACH_PTR(member);
	out->indent--;
	append(out, "};\n");
}

struct tag_output {
	struct code_buffer *out;
	struct symbol *definitions[8192];
	struct symbol *forwards[8192];
	unsigned int definition_count;
	unsigned int forward_count;
};

static void show_struct_definition(struct tag_output *output,
				   struct symbol *type);
static void show_type_definition(struct tag_output *output,
				 struct symbol *type);

static bool tag_was_output(struct symbol **tags, unsigned int count,
			   struct symbol *type)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (tags[i] == type ||
		    (tags[i]->type == type->type && tags[i]->ident && type->ident &&
		     strcmp(show_ident(tags[i]->ident), show_ident(type->ident)) == 0))
			return true;
	return false;
}

static void show_required_forwards(struct tag_output *output,
				   struct symbol *type)
{
	struct struct_types types = { };
	unsigned int i;

	collect_struct_types(&types, type);
	for (i = 0; i < types.count; i++) {
		struct symbol *needed = types.types[i];

		if (needed->type == SYM_ENUM && !tag_has_definition(needed)) {
			if (!tag_was_output(output->forwards, output->forward_count,
					    needed)) {
				append(output->out, "enum %s;\n",
				       show_ident(needed->ident));
				if (output->forward_count < ARRAY_SIZE(output->forwards))
					output->forwards[output->forward_count++] = needed;
			}
			continue;
		}
		if (is_typedef_tag(needed) &&
		    !tag_was_output(output->definitions, output->definition_count,
				    needed)) {
			if (tag_has_definition(needed))
				show_type_definition(output, needed);
			else
				fprintf(stderr, "missing definition for %s %s\n",
					tag_type_name(needed), show_ident(needed->ident));
			continue;
		}
		if (types.needs_forward[i] &&
		    !tag_was_output(output->definitions, output->definition_count, needed) &&
		    !tag_was_output(output->forwards, output->forward_count, needed)) {
			append(output->out, "%s %s;\n", tag_type_name(needed),
			       show_ident(needed->ident));
			if (output->forward_count < ARRAY_SIZE(output->forwards))
				output->forwards[output->forward_count++] = needed;
		}
		if (!types.needs_forward[i] &&
		    !tag_was_output(output->definitions, output->definition_count,
				    needed)) {
			if (tag_has_definition(needed))
				show_type_definition(output, needed);
			else
				fprintf(stderr, "missing definition for %s %s\n",
					tag_type_name(needed), show_ident(needed->ident));
		}
	}
}

static void show_function_forwards(struct code_buffer *out, struct symbol *type)
{
	struct struct_types types = { };
	struct tag_output output = { .out = out };
	unsigned int i;

	handle_functions(&types, type);
	for (i = 0; i < types.count; i++) {
		struct symbol *needed = types.types[i];

		if (needed->type == SYM_ENUM && !tag_has_definition(needed)) {
			if (!tag_was_output(output.forwards, output.forward_count,
					    needed)) {
				append(out, "enum %s;\n", show_ident(needed->ident));
				if (output.forward_count < ARRAY_SIZE(output.forwards))
					output.forwards[output.forward_count++] = needed;
			}
			continue;
		}
		if (types.needs_forward[i] &&
		    !tag_was_output(output.forwards, output.forward_count, needed)) {
			append(out, "%s %s;\n", tag_type_name(needed),
			       show_ident(needed->ident));
			if (output.forward_count < ARRAY_SIZE(output.forwards))
				output.forwards[output.forward_count++] = needed;
		}
		if (!types.needs_forward[i] && !tag_has_definition(needed))
			fprintf(stderr, "missing definition for %s %s\n",
				tag_type_name(needed), show_ident(needed->ident));
	}
}

static void add_symbol_dependency(struct symbol **functions, unsigned int *count,
				  struct symbol *symbol)
{
	struct symbol *type;
	unsigned int i;

	if (!symbol || !symbol->ident ||
	    strncmp(symbol->ident->name, "__builtin_", 10) == 0)
		return;
	type = strip_type_wrappers(symbol);
	if (!type ||
	    (type->type != SYM_FN &&
	     !(symbol->ctype.modifiers & (MOD_TOPLEVEL | MOD_EXTERN))))
		return;
	for (i = 0; i < *count; i++)
		if (functions[i] == symbol)
			return;
	if (*count < 8192)
		functions[(*count)++] = symbol;
}

static void collect_expression_functions(struct symbol **functions,
					 unsigned int *count, struct expression *e)
{
	struct expression *item;
	struct type_expression *map;

	if (!e)
		return;
	switch (e->type) {
	case EXPR_TYPE:
		collect_type_expression_functions(functions, count, e->symbol);
		break;
	case EXPR_PREOP:
	case EXPR_POSTOP:
		collect_expression_functions(functions, count, e->unop);
		break;
	case EXPR_BINOP:
	case EXPR_ASSIGNMENT:
	case EXPR_LOGICAL:
	case EXPR_COMMA:
	case EXPR_COMPARE:
		collect_expression_functions(functions, count, e->left);
		collect_expression_functions(functions, count, e->right);
		break;
	case EXPR_DEREF:
		collect_expression_functions(functions, count, e->deref);
		break;
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
	case EXPR_SIZEOF:
	case EXPR_ALIGNOF:
	case EXPR_PTRSIZEOF:
		collect_type_expression_functions(functions, count, e->cast_type);
		collect_expression_functions(functions, count, e->cast_expression);
		break;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		collect_expression_functions(functions, count, e->conditional);
		collect_expression_functions(functions, count, e->cond_true);
		collect_expression_functions(functions, count, e->cond_false);
		break;
	case EXPR_STATEMENT:
		collect_statement_functions(functions, count, e->statement);
		break;
	case EXPR_CALL:
		item = e->fn;
		while (item && item->type == EXPR_PREOP &&
		       (item->op == '*' || item->op == '('))
			item = item->unop;
		if (item && item->type == EXPR_SYMBOL)
			add_symbol_dependency(functions, count, item->symbol);
		collect_expression_functions(functions, count, e->fn);
		FOR_EACH_PTR(e->args, item) {
			collect_expression_functions(functions, count, item);
		} END_FOR_EACH_PTR(item);
		break;
	case EXPR_SYMBOL:
		add_symbol_dependency(functions, count, e->symbol);
		break;
	case EXPR_INITIALIZER:
		FOR_EACH_PTR(e->expr_list, item) {
			collect_expression_functions(functions, count, item);
		} END_FOR_EACH_PTR(item);
		break;
	case EXPR_IDENTIFIER:
		collect_expression_functions(functions, count, e->ident_expression);
		break;
	case EXPR_INDEX:
		collect_expression_functions(functions, count, e->idx_expression);
		break;
	case EXPR_POS:
		collect_expression_functions(functions, count, e->init_expr);
		break;
	case EXPR_SLICE:
		collect_expression_functions(functions, count, e->base);
		break;
	case EXPR_OFFSETOF:
		for (item = e; item; item = item->down) {
			if (item->op != '.')
				collect_expression_functions(functions, count, item->index);
		}
		break;
	case EXPR_GENERIC:
		collect_expression_functions(functions, count, e->control);
		for (map = e->map; map; map = map->next) {
			collect_expression_functions(functions, count, map->expr);
		}
		collect_expression_functions(functions, count, e->def);
		break;
	default:
		break;
	}
}

static void collect_type_expression_functions(struct symbol **functions,
					      unsigned int *count,
					      struct symbol *type)
{
	while (type) {
		if (type->type == SYM_TYPEOF ||
		    type->type == SYM_TYPEOF_UNQUAL)
			collect_expression_functions(functions, count,
						     type->initializer);
		if (type->type_expression)
			collect_expression_functions(functions, count,
						     type->type_expression);
		type = type->ctype.base_type;
	}
}

static void collect_declaration_functions(struct symbol **functions,
					  unsigned int *count,
					  struct symbol_list *declarations)
{
	struct symbol *declaration;

	FOR_EACH_PTR(declarations, declaration) {
		collect_type_expression_functions(functions, count, declaration);
		collect_expression_functions(functions, count, declaration->initializer);
	} END_FOR_EACH_PTR(declaration);
}

static void show_initializer_dependencies_rec(struct code_buffer *out,
					      struct symbol *symbol,
					      struct symbol **active,
					      unsigned int active_count)
{
	struct symbol *dependencies[8192];
	struct symbol *dependency;
	struct symbol *type;
	struct symbol copy;
	unsigned int count = 0;
	unsigned int i;

	for (i = 0; i < active_count; i++)
		if (active[i] == symbol)
			return;
	if (active_count >= 8192)
		return;
	active[active_count++] = symbol;
	collect_type_expression_functions(dependencies, &count, symbol);
	collect_expression_functions(dependencies, &count, symbol->initializer);
	for (i = 0; i < count; i++) {
		dependency = dependencies[i];
		if (dependency == symbol || find_anonymous_tag(dependency))
			continue;
		show_initializer_dependencies_rec(out, dependency, active,
						  active_count);
		type = strip_type_wrappers(dependency);
		if (type->type == SYM_FN)
			show_function_forwards(out, dependency->ctype.base_type);
		copy = *dependency;
		copy.ctype.modifiers &= ~MOD_STATIC;
		copy.ctype.modifiers |= MOD_EXTERN;
		show_c_typename(out, &copy);
		show_asm_name(out, &copy);
		append(out, ";\n");
	}
}

static void show_initializer_dependencies(struct code_buffer *out,
					  struct symbol *symbol)
{
	struct symbol *active[8192];

	show_initializer_dependencies_rec(out, symbol, active, 0);
}

static void collect_asm_functions(struct symbol **functions, unsigned int *count,
				  struct asm_operand_list *operands)
{
	struct asm_operand *operand;

	FOR_EACH_PTR(operands, operand) {
		collect_expression_functions(functions, count, operand->expr);
	} END_FOR_EACH_PTR(operand);
}

static void collect_statement_functions(struct symbol **functions,
					unsigned int *count, struct statement *s)
{
	struct statement *child;
	struct expression *clobber;

	if (!s)
		return;
	switch (s->type) {
	case STMT_COMPOUND:
		FOR_EACH_PTR(s->stmts, child) {
			collect_statement_functions(functions, count, child);
		} END_FOR_EACH_PTR(child);
		break;
	case STMT_DECLARATION:
		collect_declaration_functions(functions, count, s->declaration);
		break;
	case STMT_EXPRESSION:
	case STMT_CONTEXT:
		collect_expression_functions(functions, count, s->expression);
		break;
	case STMT_RETURN:
		collect_expression_functions(functions, count, s->ret_value);
		break;
	case STMT_IF:
		collect_expression_functions(functions, count, s->if_conditional);
		collect_statement_functions(functions, count, s->if_true);
		collect_statement_functions(functions, count, s->if_false);
		break;
	case STMT_SWITCH:
		collect_expression_functions(functions, count, s->switch_expression);
		collect_statement_functions(functions, count, s->switch_statement);
		break;
	case STMT_CASE:
		collect_expression_functions(functions, count, s->case_expression);
		collect_expression_functions(functions, count, s->case_to);
		collect_statement_functions(functions, count, s->case_statement);
		break;
	case STMT_ITERATOR:
		collect_declaration_functions(functions, count, s->iterator_syms);
		collect_statement_functions(functions, count, s->iterator_pre_statement);
		collect_expression_functions(functions, count, s->iterator_pre_condition);
		collect_statement_functions(functions, count, s->iterator_statement);
		collect_statement_functions(functions, count, s->iterator_post_statement);
		collect_expression_functions(functions, count, s->iterator_post_condition);
		break;
	case STMT_LABEL:
		collect_statement_functions(functions, count, s->label_statement);
		break;
	case STMT_GOTO:
		collect_expression_functions(functions, count, s->goto_expression);
		break;
	case STMT_ASM:
		collect_expression_functions(functions, count, s->asm_string);
		collect_asm_functions(functions, count, s->asm_outputs);
		collect_asm_functions(functions, count, s->asm_inputs);
		FOR_EACH_PTR(s->asm_clobbers, clobber) {
			collect_expression_functions(functions, count, clobber);
		} END_FOR_EACH_PTR(clobber);
		break;
	case STMT_RANGE:
		collect_expression_functions(functions, count, s->range_expression);
		collect_expression_functions(functions, count, s->range_low);
		collect_expression_functions(functions, count, s->range_high);
		break;
	default:
		break;
	}
}

static void show_function_dependencies(struct code_buffer *out,
				       struct symbol *function)
{
	struct symbol *functions[8192];
	struct symbol *dependency;
	struct symbol copy;
	struct symbol *type;
	unsigned int count = 0;
	unsigned int i;

	collect_type_expression_functions(functions, &count, function);
	collect_statement_functions(functions, &count,
					function->ctype.base_type->stmt);
	for (i = 0; i < count; i++) {
		dependency = functions[i];
		if (dependency == function || find_anonymous_tag(dependency))
			continue;
		show_initializer_dependencies(out, dependency);
		type = strip_type_wrappers(dependency);
		if (type->type == SYM_FN) {
			show_function_forwards(out, dependency->ctype.base_type);
			copy = *dependency;
			copy.ctype.modifiers &= ~(MOD_STATIC | MOD_INLINE | MOD_GNU_INLINE);
			copy.ctype.modifiers |= MOD_EXTERN;
			dependency = &copy;
		} else {
			copy = *dependency;
			copy.ctype.modifiers &= ~MOD_STATIC;
			copy.ctype.modifiers |= MOD_EXTERN;
			dependency = &copy;
		}
		show_c_typename(out, dependency);
		show_asm_name(out, dependency);
		append(out, ";\n");
	}
}

static void show_function(struct code_buffer *out, struct symbol *s)
{
	struct symbol *type = s->ctype.base_type;
	struct symbol copy;

	show_function_forwards(out, type);
	show_function_dependencies(out, s);
	if (type->stmt) {
		copy = *s;
		if (!output_whole_file) {
			copy.ctype.modifiers &= ~MOD_EXTERN;
			copy.ctype.modifiers |= MOD_STATIC;
		}
		show_c_typename(out, &copy);
		append(out, "\n");
		function_body(out, type->stmt);
	} else {
		show_c_typename(out, s);
		append(out, ";\n");
	}
}

static void show_struct_definition(struct tag_output *output, struct symbol *type)
{
	struct code_buffer *out = output->out;
	struct symbol *member;
	struct symbol *anonymous_tag;

	show_required_forwards(output, type);
	if (is_typedef_tag(type)) {
		append(out, "typedef %s%s%s {\n", tag_type_name(type),
		       type->namespace == NS_STRUCT ? " " : "",
		       type->namespace == NS_STRUCT ? show_ident(type->ident) : "");
	} else {
		append(out, "%s %s {\n", tag_type_name(type), show_ident(type->ident));
	}
	out->indent++;
	FOR_EACH_PTR(type->symbol_list, member) {
		anonymous_tag = find_anonymous_tag(member);
		if (anonymous_tag) {
			show_anonymous_c_typename(out, member, anonymous_tag);
			append(out, ";\n");
			continue;
		}
		show_c_typename(out, member);
		append(out, ";\n");
	} END_FOR_EACH_PTR(member);
	out->indent--;
	if (is_typedef_tag(type))
		append(out, "} %s;\n", show_ident(type->ident));
	else
		append(out, "};\n");
	if (output->definition_count < ARRAY_SIZE(output->definitions))
		output->definitions[output->definition_count++] = type;
}

static void show_enum_definition(struct tag_output *output, struct symbol *type)
{
	struct code_buffer *out = output->out;
	struct symbol *member;

	if (is_typedef_tag(type))
		append(out, "typedef enum%s%s {\n",
		       type->namespace == NS_STRUCT ? " " : "",
		       type->namespace == NS_STRUCT ? show_ident(type->ident) : "");
	else
		append(out, "enum %s {\n", show_ident(type->ident));
	out->indent++;
	FOR_EACH_PTR(type->symbol_list, member) {
		append(out, "%s", show_ident(member->ident));
		if (member->initializer) {
			append(out, " = ");
			expr(out, member->initializer);
		}
		append(out, ",\n");
	} END_FOR_EACH_PTR(member);
	out->indent--;
	if (is_typedef_tag(type))
		append(out, "} %s;\n", show_ident(type->ident));
	else
		append(out, "};\n");
	if (output->definition_count < ARRAY_SIZE(output->definitions))
		output->definitions[output->definition_count++] = type;
}

static void show_type_definition(struct tag_output *output, struct symbol *type)
{
	if (type->type == SYM_ENUM)
		show_enum_definition(output, type);
	else
		show_struct_definition(output, type);
}

static void show_typedef_definition(struct code_buffer *out, struct symbol *type)
{
	show_function_forwards(out, strip_type_wrappers(type));
	append(out, "typedef ");
	show_c_typename(out, type);
	append(out, ";\n");
}

void show_all_type_definitions(struct symbol_list *symbols, char *buf, size_t len)
{
	struct code_buffer out = { .buf = buf, .len = len, .line_start = true };
	struct tag_output output = { .out = &out };
	struct symbol *symbol;

	if (len)
		buf[0] = '\0';

	FOR_EACH_PTR(symbols, symbol) {
		struct symbol *tag;

		if (is_type_definition(symbol, &tag)) {
			if (!tag_was_output(output.definitions,
					    output.definition_count, tag))
				show_type_definition(&output, tag);
		} else if (symbol->namespace == NS_TYPEDEF && symbol->ident &&
			   lookup_symbol(symbol->ident, NS_TYPEDEF) == symbol) {
			show_typedef_definition(&out, symbol);
		}
	} END_FOR_EACH_PTR(symbol);
}

void show_struct_declarations(struct symbol_list *symbols, struct symbol *last,
			      char *buf, size_t len)
{
	struct code_buffer out = {
		.buf = buf,
		.len = len,
		.line_start = true,
	};
	struct struct_types types = { };
	struct tag_output output = { .out = &out };
	struct symbol *sym;
	unsigned int i;

	if (len)
		buf[0] = 0;
	FOR_EACH_PTR(symbols, sym) {
		collect_struct_types(&types, sym);
		if (sym == last)
			break;
	} END_FOR_EACH_PTR(sym);
	FOR_EACH_PTR(symbols, sym) {
		for (i = 0; i < types.count; i++) {
			struct symbol *type = types.types[i];

			if (!tag_has_definition(type) ||
			    tag_was_output(output.definitions, output.definition_count, type) ||
			    !symbol_defines_tag(sym, type))
				continue;
			show_struct_definition(&output, type);
		}
	} END_FOR_EACH_PTR(sym);
}

void show_expr(struct expression *e, char *buf, size_t len)
{
	struct code_buffer out = { .buf = buf, .len = len, .line_start = true };

	if (len)
		buf[0] = 0;

	expr(&out, e);
}

void show_stmt(struct statement *s, char *buf, size_t len)
{
	struct code_buffer out = { .buf = buf, .len = len, .line_start = true };

	if (len)
		buf[0] = 0;
	stmt(&out, s);
}

void show_sym(struct symbol *s, char *buf, size_t len)
{
	struct code_buffer out = { .buf = buf, .len = len, .line_start = true };

	if (len)
		buf[0] = 0;
	sym(&out, s);
}
