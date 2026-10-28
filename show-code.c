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
#include "utils.h"

struct code_buffer {
	char *buf;
	size_t len, used;
	unsigned int indent;
	bool line_start;
	struct symbol *local_tags[1024];
	unsigned int local_tag_count;
};

static struct expression *rendering_expression;

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
static void declaration(struct code_buffer *out, struct symbol *s);
static void show_function(struct code_buffer *out, struct symbol *s);
static bool is_typedef_tag(struct symbol *type);
static struct symbol *find_anonymous_tag(struct symbol *type);
static void show_anonymous_c_typename(struct code_buffer *out,
				      struct symbol *type,
				      struct symbol *anonymous_tag);
static void show_c_typename(struct code_buffer *out, struct symbol *s);
static void show_asm_name(struct code_buffer *out, struct symbol *s);
static void show_initializer_dependencies(struct code_buffer *out,
					  struct symbol *symbol);
static bool show_inline_function_for_symbol(struct code_buffer *out,
					    struct symbol *function);
static bool is_type_definition(struct symbol *s, struct symbol **tag_out);
static bool typedef_was_output(struct symbol *type);
static bool tag_has_definition(struct symbol *tag);
static void show_local_tag_definition(struct code_buffer *out,
				      struct symbol *tag);
static bool expression_references_symbol(struct expression *e,
					 struct symbol *symbol, unsigned int depth);
static bool is_struct_or_union(struct symbol *type);
static bool is_tag_type(struct symbol *type);
static const char *tag_type_name(struct symbol *type);
static bool has_type_expression(struct symbol *type);
static struct symbol *show_base_type(struct symbol *type);
struct struct_types;
struct symbol_dependencies;
static void collect_statement_dependencies(struct symbol_dependencies *dependencies,
						   struct statement *s);
static void collect_type_expression_dependencies(struct symbol_dependencies *dependencies,
							   struct symbol *type);

struct struct_types {
	struct symbol *types[8192];
	bool needs_forward[8192];
	unsigned int count;
};

struct symbol_dependencies {
	struct struct_types types;
	struct symbol *symbols[8192];
	unsigned int symbol_count;
};

struct c_type_name {
	char *start;
	char *end;
};

#define C_TYPE_NAME_BUFFER_SIZE 16384
#define C_TYPE_NAME_BUFFER_MIDDLE (C_TYPE_NAME_BUFFER_SIZE / 2)

/* Dependency collection and emission. */

static struct expression *symbol_initializer(struct symbol *symbol)
{
	return symbol->orig_initializer ?: symbol->initializer;
}

static bool tag_has_definition(struct symbol *tag)
{
	return is_tag_type(tag) && tag->endpos.type;
}

static bool is_type_definition(struct symbol *s, struct symbol **tag_out)
{
	struct symbol *tag;

	for (tag = s; tag; tag = tag->ctype.base_type) {
		if (tag->type == SYM_FN)
			return false;
		if (is_tag_type(tag))
			break;
	}
	if (!tag_has_definition(tag) ||
	    (s != tag && s->definition != tag && s->namespace != NS_TYPEDEF))
		return false;
	*tag_out = tag;
	return true;
}

static bool is_struct_or_union(struct symbol *type)
{
	return type && (type->type == SYM_STRUCT || type->type == SYM_UNION);
}

static bool is_tag_type(struct symbol *type)
{
	return is_struct_or_union(type) ||
	       (type && type->type == SYM_ENUM);
}

static const char *tag_type_name(struct symbol *type)
{
	switch(type->type) {
	case SYM_ENUM:
		return "enum";
	case SYM_UNION:
		return "union";
	case SYM_STRUCT:
		return "struct";
	}
	return "<unknown>";
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

static void collect_dependencies(struct struct_types *types,
				 struct symbol *type, bool needs_forward);

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

static struct symbol *type_typedef_symbol(struct symbol *type)
{
	struct symbol *typedef_symbol;

	if (!type)
		return NULL;
	if (type->type == SYM_RESTRICT && type->ident) {
		typedef_symbol = lookup_symbol(type->ident, NS_TYPEDEF);
		if (typedef_symbol != type)
			return typedef_symbol;
	}
	for (type = get_base_type(type); type &&
	     (type->type == SYM_NODE || type->type == SYM_RESTRICT ||
	      type->type == SYM_BITFIELD); type = get_base_type(type)) {
		if (type->type == SYM_RESTRICT && type->ident) {
			typedef_symbol = lookup_symbol(type->ident, NS_TYPEDEF);
			if (typedef_symbol)
				return typedef_symbol;
		}
	}
	return NULL;
}

static bool type_is_ptr(struct symbol *type)
{
	return type && type->type == SYM_PTR;
}

static bool type_is_array(struct symbol *type)
{
	return type && type->type == SYM_ARRAY;
}

static bool type_can_use_forward_declaration(struct symbol *type)
{
	type = strip_type_wrappers(type);
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
	if (type && type->definition && is_tag_type(type->definition))
		type = type->definition;
	if (!is_tag_type(type) || !type->ident)
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

static void collect_dependencies(struct struct_types *types,
				 struct symbol *type, bool needs_forward)
{
	struct symbol *struct_type;
	struct symbol *member;
	struct symbol *arg;

	if (!type)
		return;
	if (type->scope && !toplevel(type->scope))
		return;
	switch (type->type) {
	case SYM_NODE:
	case SYM_RESTRICT:
	case SYM_BITFIELD:
	case SYM_TYPEOF:
	case SYM_TYPEOF_UNQUAL:
		collect_dependencies(types, get_base_type(type), needs_forward);
		return;
	case SYM_PTR:
	case SYM_ARRAY:
		collect_dependencies(types, get_base_type(type),
				     type_can_use_forward_declaration(type));
		return;
	case SYM_FN:
		collect_dependencies(types, type->ctype.base_type,
				     type_can_use_forward_declaration(
					     type->ctype.base_type));
		FOR_EACH_PTR(type->arguments, arg) {
			collect_dependencies(types, arg,
					     type_can_use_forward_declaration(arg));
		} END_FOR_EACH_PTR(arg);
		return;
	case SYM_ENUM:
		add_struct_type(types, type, needs_forward);
		return;
	case SYM_STRUCT:
	case SYM_UNION:
		struct_type = add_struct_type(types, type, needs_forward);
		if (!struct_type)
			struct_type = type;
		if (needs_forward || !struct_type->symbol_list)
			return;
		FOR_EACH_PTR(struct_type->symbol_list, member) {
			collect_dependencies(types, member,
					     type_can_use_forward_declaration(member));
		} END_FOR_EACH_PTR(member);
		return;
	default:
		return;
	}
}

static void collect_function_type_dependencies(struct symbol_dependencies *dependencies,
					       struct symbol *type)
{
	collect_dependencies(&dependencies->types, type,
			     type_can_use_forward_declaration(type));
}

struct output_dependencies {
	struct symbol_list *type_definitions;
	struct symbol_list *type_forwards;
	struct symbol_list *typedefs;
	struct symbol_list *function_definitions;
	struct symbol_list *function_forwards;
	struct symbol_list *data_definitions;
};

static struct output_dependencies printed;

struct tag_output {
	struct code_buffer *out;
	struct symbol_list *dependency_types;
};

static void show_struct_definition(struct tag_output *output,
				   struct symbol *type);
static void show_tag_definition(struct tag_output *output,
				struct symbol *type);
static void show_type_definition(struct tag_output *output,
				 struct symbol *symbol);
static void show_typedef_definition(struct code_buffer *out,
				    struct symbol *type);
static void show_type_dependencies(struct tag_output *output,
				   struct symbol *type, bool needs_forward);
static void show_type_members(struct tag_output *output,
			      struct symbol *type);

static bool tag_was_output(struct symbol_list *tags, struct symbol *type)
{
	struct symbol *tag;

	FOR_EACH_PTR(tags, tag) {
		if (tag == type ||
		    (tag->type == type->type && tag->ident && type->ident &&
		     strcmp(show_ident(tag->ident), show_ident(type->ident)) == 0))
			return true;
	} END_FOR_EACH_PTR(tag);
	return false;
}

static void remember_tag(struct symbol_list **tags, struct symbol *type)
{
	if (!tag_was_output(*tags, type))
		add_symbol(tags, type);
}

static bool typedef_was_output(struct symbol *type)
{
	return tag_was_output(printed.typedefs, type);
}

static bool symbol_was_output(struct symbol_list *symbols,
			      struct symbol *symbol)
{
	struct symbol *entry;

	FOR_EACH_PTR(symbols, entry) {
		if (entry == symbol)
			return true;
		if (entry->ident && symbol->ident &&
		    strcmp(show_ident(entry->ident), show_ident(symbol->ident)) == 0)
			return true;
	} END_FOR_EACH_PTR(entry);
	return false;
}

static void remember_symbol(struct symbol_list **symbols,
			    struct symbol *symbol)
{
	if (!symbol_was_output(*symbols, symbol))
		add_symbol(symbols, symbol);
}

static void show_required_type(struct tag_output *output,
				       struct symbol *needed, bool needs_forward)
{
	if (needed->type == SYM_ENUM && !tag_has_definition(needed)) {
		if (!tag_was_output(printed.type_forwards, needed)) {
			append(output->out, "enum %s;\n", show_ident(needed->ident));
			remember_tag(&printed.type_forwards, needed);
		}
		return;
	}
	if (is_typedef_tag(needed) || !needs_forward) {
		if (!tag_was_output(printed.type_definitions, needed)) {
			if (tag_has_definition(needed))
				show_type_definition(output, needed);
			else
				fprintf(stderr, "missing definition for %s %s\n",
					tag_type_name(needed), show_ident(needed->ident));
		}
		return;
	}
	if (!tag_was_output(printed.type_definitions, needed) &&
	    !tag_was_output(printed.type_forwards, needed)) {
		append(output->out, "%s %s;\n", tag_type_name(needed),
		       show_ident(needed->ident));
		remember_tag(&printed.type_forwards, needed);
	}
}

static void show_function_forwards(struct code_buffer *out, struct symbol *type)
{
	struct symbol_dependencies dependencies = { };
	struct tag_output output = { .out = out };
	unsigned int i;

	show_type_dependencies(&output, type, true);
	collect_function_type_dependencies(&dependencies, type);
	for (i = 0; i < dependencies.types.count; i++) {
		struct symbol *needed = dependencies.types.types[i];

		if (needed->type == SYM_ENUM && !tag_has_definition(needed)) {
			if (!tag_was_output(printed.type_forwards, needed)) {
				append(out, "enum %s;\n", show_ident(needed->ident));
				remember_tag(&printed.type_forwards, needed);
			}
			continue;
		}
		if (dependencies.types.needs_forward[i] &&
		    !tag_was_output(printed.type_definitions, needed) &&
		    !tag_was_output(printed.type_forwards, needed)) {
			append(out, "%s %s;\n", tag_type_name(needed),
			       show_ident(needed->ident));
			remember_tag(&printed.type_forwards, needed);
		}
		if (!dependencies.types.needs_forward[i] && !tag_has_definition(needed))
			fprintf(stderr, "missing definition for %s %s\n",
				tag_type_name(needed), show_ident(needed->ident));
	}
}

static void add_dependency_symbol(struct symbol **symbols, unsigned int *count,
				  struct symbol *symbol)
{
	struct symbol *type;
	unsigned int i;

	if (!symbol || !symbol->ident ||
	    strncmp(symbol->ident->name, "__builtin_", 10) == 0)
		return;
	type = strip_type_wrappers(symbol);
	if (!type || (type->type != SYM_FN && symbol->scope != file_scope &&
		      !(symbol->ctype.modifiers & (MOD_TOPLEVEL | MOD_EXTERN))))
		return;
	for (i = 0; i < *count; i++)
		if (symbols[i] == symbol)
			return;
	if (*count < 8192)
		symbols[(*count)++] = symbol;
}

static struct symbol *member_type(struct symbol *type, struct ident *ident)
{
	struct symbol *member;

	type = strip_type_wrappers(type);
	if (type && type->definition && is_tag_type(type->definition))
		type = type->definition;
	if (!is_tag_type(type))
		return NULL;
	FOR_EACH_PTR(type->symbol_list, member) {
		if (member->ident && ident &&
		    strcmp(show_ident(member->ident), show_ident(ident)) == 0)
			return member->ctype.base_type;
	} END_FOR_EACH_PTR(member);
	return NULL;
}

/* Return the type named by an unevaluated member-access expression. */
static struct symbol *member_access_type(struct expression *e)
{
	struct symbol *type;

	if (!e)
		return NULL;
	switch (e->type) {
	case EXPR_SYMBOL:
		return e->symbol ? e->symbol->ctype.base_type : NULL;
	case EXPR_CALL:
		type = strip_type_wrappers(member_access_type(e->fn));
		while (type_is_ptr(type))
			type = strip_type_wrappers(get_base_type(type));
		return type && type->type == SYM_FN ? type->ctype.base_type :
			e->ctype;
	case EXPR_PREOP:
		if (e->op == '(')
			return member_access_type(e->unop);
		if (e->op != '*')
			return NULL;
		type = strip_type_wrappers(member_access_type(e->unop));
		return type_is_ptr(type) ? get_base_type(type) : NULL;
	case EXPR_DEREF:
		type = member_access_type(e->deref);
		return e->member ? member_type(type, e->member) : type;
	default:
		return NULL;
	}
}

/* Return the aggregate whose member access requires its definition. */
static struct symbol *member_access_container_type(struct expression *e)
{
	struct symbol *type = strip_type_wrappers(member_access_type(e));

	if (type_is_ptr(type))
		type = get_base_type(type);
	return type;
}

static void collect_expression_dependencies(struct symbol_dependencies *dependencies,
						    struct expression *e)
{
	struct expression *item;
	struct type_expression *map;
	struct symbol **symbols = dependencies->symbols;
	unsigned int *count = &dependencies->symbol_count;

	if (!e)
		return;
	switch (e->type) {
	case EXPR_TYPE:
		collect_dependencies(&dependencies->types, e->symbol, false);
		collect_type_expression_dependencies(dependencies, e->symbol);
		break;
	case EXPR_PREOP:
	case EXPR_POSTOP:
		collect_expression_dependencies(dependencies, e->unop);
		break;
	case EXPR_BINOP:
	case EXPR_ASSIGNMENT:
	case EXPR_LOGICAL:
	case EXPR_COMMA:
	case EXPR_COMPARE:
		collect_expression_dependencies(dependencies, e->left);
		collect_expression_dependencies(dependencies, e->right);
		break;
	case EXPR_DEREF:
		if (e->member && e->deref) {
			struct symbol *type = member_access_container_type(e->deref);

			collect_dependencies(&dependencies->types, type, false);
		}
		collect_expression_dependencies(dependencies, e->deref);
		break;
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
	case EXPR_SIZEOF:
	case EXPR_ALIGNOF:
	case EXPR_PTRSIZEOF:
		collect_dependencies(&dependencies->types, e->cast_type, false);
		collect_type_expression_dependencies(dependencies, e->cast_type);
		collect_expression_dependencies(dependencies, e->cast_expression);
		break;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		collect_expression_dependencies(dependencies, e->conditional);
		collect_expression_dependencies(dependencies, e->cond_true);
		collect_expression_dependencies(dependencies, e->cond_false);
		break;
	case EXPR_STATEMENT:
		collect_statement_dependencies(dependencies, e->statement);
		break;
	case EXPR_CALL:
		item = e->fn;
		while (item && item->type == EXPR_PREOP &&
		       (item->op == '*' || item->op == '('))
			item = item->unop;
		if (item && item->type == EXPR_SYMBOL)
			add_dependency_symbol(symbols, count, item->symbol);
		collect_expression_dependencies(dependencies, e->fn);
		FOR_EACH_PTR(e->args, item) {
			collect_expression_dependencies(dependencies, item);
		} END_FOR_EACH_PTR(item);
		break;
	case EXPR_SYMBOL:
		add_dependency_symbol(symbols, count, e->symbol);
		break;
	case EXPR_INITIALIZER:
		FOR_EACH_PTR(e->expr_list, item) {
			collect_expression_dependencies(dependencies, item);
		} END_FOR_EACH_PTR(item);
		break;
	case EXPR_IDENTIFIER:
		collect_expression_dependencies(dependencies, e->ident_expression);
		break;
	case EXPR_INDEX:
		collect_expression_dependencies(dependencies, e->idx_expression);
		break;
	case EXPR_POS:
		collect_expression_dependencies(dependencies, e->init_expr);
		break;
	case EXPR_SLICE:
		collect_expression_dependencies(dependencies, e->base);
		break;
	case EXPR_OFFSETOF:
		for (item = e; item; item = item->down) {
			if (item->op != '.')
				collect_expression_dependencies(dependencies, item->index);
		}
		break;
	case EXPR_GENERIC:
		collect_expression_dependencies(dependencies, e->control);
		for (map = e->map; map; map = map->next) {
			collect_expression_dependencies(dependencies, map->expr);
		}
		collect_expression_dependencies(dependencies, e->def);
		break;
	default:
		break;
	}
}

static void collect_type_expression_dependencies(struct symbol_dependencies *dependencies,
							   struct symbol *type)
{
	while (type) {
		if (has_type_expression(type))
			collect_expression_dependencies(dependencies,
							type->type_expression);
		type = show_base_type(type);
	}
}

static void collect_declaration_dependencies(struct symbol_dependencies *dependencies,
						     struct symbol_list *declarations)
{
	struct symbol *declaration;

	FOR_EACH_PTR(declarations, declaration) {
		collect_dependencies(&dependencies->types, declaration->ctype.base_type,
				     type_can_use_forward_declaration(declaration));
		collect_type_expression_dependencies(dependencies, declaration);
		collect_expression_dependencies(dependencies,
						symbol_initializer(declaration));
	} END_FOR_EACH_PTR(declaration);
}

static void show_initializer_dependencies_rec(struct code_buffer *out,
					      struct symbol *symbol,
					      struct symbol **active,
					      unsigned int active_count)
{
	struct symbol_dependencies dependencies = { };
	struct symbol *dependency;
	struct symbol *type;
	struct symbol copy;
	unsigned int i;

	for (i = 0; i < active_count; i++)
		if (active[i] == symbol)
			return;
	if (active_count >= 8192)
		return;
	active[active_count++] = symbol;
	collect_type_expression_dependencies(&dependencies, symbol);
	collect_expression_dependencies(&dependencies, symbol_initializer(symbol));
	for (i = 0; i < dependencies.symbol_count; i++) {
		dependency = dependencies.symbols[i];
		if (dependency == symbol)
			dependency = dependency->same_symbol;
		if (!dependency)
			continue;
		if (symbol_was_output(printed.function_definitions, dependency) ||
		    symbol_was_output(printed.function_forwards, dependency) ||
		    symbol_was_output(printed.data_definitions, dependency))
			continue;
		if (show_inline_function_for_symbol(out, dependency))
			continue;
		show_initializer_dependencies_rec(out, dependency, active,
						  active_count);
		type = strip_type_wrappers(dependency);
		if (type->type == SYM_FN)
			show_function_forwards(out, dependency->ctype.base_type);
		if (type_is_array(type) && !type->array_size &&
		    symbol_initializer(dependency)) {
			declaration(out, dependency);
			remember_symbol(&printed.data_definitions, dependency);
			continue;
		}
		copy = *dependency;
		if (!(copy.ctype.modifiers & MOD_STATIC))
			copy.ctype.modifiers |= MOD_EXTERN;
		show_c_typename(out, &copy);
		show_asm_name(out, &copy);
		append(out, ";\n");
		remember_symbol(&printed.function_forwards, dependency);
	}
}

static void show_initializer_dependencies(struct code_buffer *out,
					  struct symbol *symbol)
{
	struct symbol *active[8192];

	show_initializer_dependencies_rec(out, symbol, active, 0);
}

static void collect_asm_dependencies(struct symbol_dependencies *dependencies,
				     struct asm_operand_list *operands)
{
	struct asm_operand *operand;

	FOR_EACH_PTR(operands, operand) {
		collect_expression_dependencies(dependencies, operand->expr);
	} END_FOR_EACH_PTR(operand);
}

static void collect_statement_dependencies(struct symbol_dependencies *dependencies,
					   struct statement *s)
{
	struct statement *child;
	struct expression *clobber;

	if (!s)
		return;
	switch (s->type) {
	case STMT_COMPOUND:
		FOR_EACH_PTR(s->stmts, child) {
			collect_statement_dependencies(dependencies, child);
		} END_FOR_EACH_PTR(child);
		break;
	case STMT_DECLARATION:
		collect_declaration_dependencies(dependencies, s->declaration);
		break;
	case STMT_EXPRESSION:
	case STMT_CONTEXT:
		collect_expression_dependencies(dependencies, s->expression);
		break;
	case STMT_RETURN:
		collect_expression_dependencies(dependencies, s->ret_value);
		break;
	case STMT_IF:
		collect_expression_dependencies(dependencies, s->if_conditional);
		collect_statement_dependencies(dependencies, s->if_true);
		collect_statement_dependencies(dependencies, s->if_false);
		break;
	case STMT_SWITCH:
		collect_expression_dependencies(dependencies, s->switch_expression);
		collect_statement_dependencies(dependencies, s->switch_statement);
		break;
	case STMT_CASE:
		collect_expression_dependencies(dependencies, s->case_expression);
		collect_expression_dependencies(dependencies, s->case_to);
		collect_statement_dependencies(dependencies, s->case_statement);
		break;
	case STMT_ITERATOR:
		collect_declaration_dependencies(dependencies, s->iterator_syms);
		collect_statement_dependencies(dependencies, s->iterator_pre_statement);
		collect_expression_dependencies(dependencies, s->iterator_pre_condition);
		collect_statement_dependencies(dependencies, s->iterator_statement);
		collect_statement_dependencies(dependencies, s->iterator_post_statement);
		collect_expression_dependencies(dependencies, s->iterator_post_condition);
		break;
	case STMT_LABEL:
		collect_statement_dependencies(dependencies, s->label_statement);
		break;
	case STMT_GOTO:
		collect_expression_dependencies(dependencies, s->goto_expression);
		break;
	case STMT_ASM:
		collect_expression_dependencies(dependencies, s->asm_string);
		collect_asm_dependencies(dependencies, s->asm_outputs);
		collect_asm_dependencies(dependencies, s->asm_inputs);
		FOR_EACH_PTR(s->asm_clobbers, clobber) {
			collect_expression_dependencies(dependencies, clobber);
		} END_FOR_EACH_PTR(clobber);
		break;
	case STMT_RANGE:
		collect_expression_dependencies(dependencies, s->range_expression);
		collect_expression_dependencies(dependencies, s->range_low);
		collect_expression_dependencies(dependencies, s->range_high);
		break;
	default:
		break;
	}
}

static void collect_function_dependencies(struct symbol_dependencies *dependencies,
					  struct symbol *function)
{
	struct symbol *type = function->ctype.base_type;

	if (!type)
		return;
	collect_function_type_dependencies(dependencies, type);
	collect_type_expression_dependencies(dependencies, function);
	collect_statement_dependencies(dependencies,
				      type->stmt ? type->stmt : type->inline_stmt);
}

static void show_function_dependencies(struct code_buffer *out,
				       struct symbol *function)
{
	struct symbol_dependencies dependencies = { };
	struct tag_output type_output = { .out = out };
	struct symbol *dependency;
	struct symbol copy;
	struct symbol *type;
	unsigned int i;

	collect_function_dependencies(&dependencies, function);
	for (i = 0; i < dependencies.types.count; i++)
		show_required_type(&type_output, dependencies.types.types[i],
				   dependencies.types.needs_forward[i]);
	for (i = 0; i < dependencies.symbol_count; i++) {
		struct symbol *original;

		dependency = dependencies.symbols[i];
		if (dependency == function)
			dependency = dependency->same_symbol;
		original = dependency;
		if (!dependency)
			continue;
		if (symbol_was_output(printed.function_definitions, dependency) ||
		    symbol_was_output(printed.function_forwards, dependency) ||
		    symbol_was_output(printed.data_definitions, dependency))
			continue;
		type = strip_type_wrappers(dependency);
		access_symbol(dependency);
		if (type->type == SYM_FN && type->inline_stmt) {
			show_inline_function_for_symbol(out, dependency);
			continue;
		}
		if (type->type == SYM_FN && type->stmt &&
		    (dependency->ctype.modifiers & MOD_STATIC)) {
			sym(out, dependency);
			continue;
		}
		show_type_dependencies(&type_output, dependency,
				       type_can_use_forward_declaration(dependency));
		show_initializer_dependencies(out, dependency);
		if (type->type != SYM_FN &&
		    (dependency->ctype.modifiers & MOD_STATIC)) {
			sym(out, dependency);
			continue;
		}
		if (type->type == SYM_FN) {
			show_function_forwards(out, dependency->ctype.base_type);
			copy = *dependency;
			copy.ctype.modifiers &= ~(MOD_INLINE | MOD_GNU_INLINE | MOD_EXTERN);
			if (!(copy.ctype.modifiers & MOD_STATIC))
				copy.ctype.modifiers |= MOD_EXTERN;
			dependency = &copy;
		} else {
			copy = *dependency;
			if (copy.ctype.modifiers & MOD_STATIC)
				copy.ctype.modifiers &= ~MOD_EXTERN;
			else
				copy.ctype.modifiers |= MOD_EXTERN;
			dependency = &copy;
		}
		show_c_typename(out, dependency);
		show_asm_name(out, dependency);
		append(out, ";\n");
		remember_symbol(&printed.function_forwards, original);
	}
}

static void show_type_members(struct tag_output *output, struct symbol *type)
{
	struct symbol *member;

	if (!is_tag_type(type))
		return;
	if (lookup_ptr_list_entry((struct ptr_list *)output->dependency_types,
				  type))
		return;
	add_ptr_list(&output->dependency_types, type);
	FOR_EACH_PTR(type->symbol_list, member) {
		show_type_dependencies(output, member,
				       type_can_use_forward_declaration(member));
	} END_FOR_EACH_PTR(member);
}

static void show_type_dependencies(struct tag_output *output,
				   struct symbol *type, bool needs_forward)
{
	struct symbol *arg;
	struct symbol *function_type;
	struct symbol *nested_type;
	struct symbol *needed;
	struct symbol *tag;
	struct symbol *typedef_symbol;
	struct struct_types types = { };

	if (!type)
		return;
	typedef_symbol = type_typedef_symbol(type);
	nested_type = strip_bitfield_type(type);
	if (!nested_type)
		return;
	while (type_is_ptr(nested_type) || type_is_array(nested_type))
		nested_type = strip_type_wrappers(get_base_type(nested_type));
	if (!typedef_symbol)
		typedef_symbol = nested_type->ident ?
			lookup_symbol(nested_type->ident, NS_TYPEDEF) : NULL;
	if (typedef_symbol && typedef_symbol != type) {
		if (is_type_definition(typedef_symbol, &tag)) {
			if (!tag_was_output(printed.type_definitions, tag))
				show_type_definition(output, tag);
			return;
		}
		if (tag_was_output(printed.typedefs, typedef_symbol))
			return;
		remember_tag(&printed.typedefs, typedef_symbol);
		show_type_dependencies(output, typedef_symbol->ctype.base_type,
				       true);
		show_typedef_definition(output->out, typedef_symbol);
		return;
	}

	needed = add_struct_type(&types, nested_type, needs_forward);
	if (needed) {
		show_required_type(output, needed, needs_forward);
		return;
	}
	if (is_struct_or_union(nested_type) && !nested_type->ident) {
		show_type_members(output, nested_type);
		return;
	}

	function_type = nested_type;
	while (type_is_ptr(function_type) || type_is_array(function_type))
		function_type = strip_type_wrappers(get_base_type(function_type));
	if (!function_type || function_type->type != SYM_FN)
		return;
	show_type_dependencies(output, function_type->ctype.base_type,
			       type_can_use_forward_declaration(
				       function_type->ctype.base_type));
	FOR_EACH_PTR(function_type->arguments, arg) {
		show_type_dependencies(output, arg,
				       type_can_use_forward_declaration(arg));
	} END_FOR_EACH_PTR(arg);
}

/* C rendering. */

static void type_name_prepend(struct c_type_name *name, const char *text)
{
	size_t len = strlen(text);

	name->start -= len;
	memcpy(name->start, text, len);
}

static void type_name_append(struct c_type_name *name, const char *text)
{
	size_t len = strlen(text);

	memcpy(name->end, text, len);
	name->end += len;
}

static void type_name_prepend_format(struct c_type_name *name,
					     const char *format, ...)
{
	va_list args;
	char *buffer;

	va_start(args, format);
	buffer = xvasprintf(format, args);
	va_end(args);
	type_name_prepend(name, buffer);
}

static void type_name_append_format(struct c_type_name *name,
					    const char *format, ...)
{
	va_list args;
	char *buffer;

	va_start(args, format);
	buffer = xvasprintf(format, args);
	va_end(args);
	type_name_append(name, buffer);
}

static const char *show_gcc_modifiers(unsigned long modifiers)
{
	static char buffer[1024];
	static const struct {
		unsigned long modifier;
		const char *name;
	} names[] = {
		{ MOD_AUTO, "auto" },
		{ MOD_EXTERN, "extern" },
		{ MOD_REGISTER, "register" },
		{ MOD_STATIC, "static" },
		{ MOD_INLINE, "inline" },
		{ MOD_CONST, "const" },
		{ MOD_RESTRICT, "restrict" },
		{ MOD_VOLATILE, "volatile" },
		{ MOD_ATOMIC, "_Atomic" },
		{ MOD_ALWAYS_INLINE, "__attribute__((__always_inline__))" },
		{ MOD_GNU_INLINE, "__attribute__((__gnu_inline__))" },
		{ MOD_NORETURN, "__attribute__((__noreturn__))" },
		{ MOD_PURE, "__attribute__((__pure__))" },
		{ MOD_SIGNED, "signed" },
		{ MOD_TLS, "__thread" },
		{ MOD_UNSIGNED, "unsigned" },
		{ MOD_UNUSED, "__attribute__((__unused__))" },
	};
	unsigned int i;

	buffer[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		if (!(modifiers & names[i].modifier))
			continue;
		if (buffer[0])
			strlcat(buffer, " ", sizeof(buffer));
		strlcat(buffer, names[i].name, sizeof(buffer));
	}
	if (buffer[0])
		strlcat(buffer, " ", sizeof(buffer));
	return buffer;
}

static void show_direct_tag_typename(struct c_type_name *name,
				     struct symbol *type, unsigned long modifiers)
{
	if (name->start != name->end)
		type_name_prepend(name, " ");
	if (is_typedef_tag(type))
		type_name_prepend_format(name, "%s", show_ident(type->ident));
	else
		type_name_prepend_format(name, "%s %s", tag_type_name(type),
					show_ident(type->ident));
	type_name_prepend(name, show_gcc_modifiers(modifiers &
		(MOD_QUALIFIER | MOD_ATOMIC)));
}

static bool type_references_symbol(struct symbol *type, struct symbol *symbol,
				   unsigned int depth)
{
	if (depth >= 256)
		return false;
	while (type) {
		if (type != symbol &&
		    (expression_references_symbol(type->initializer, symbol,
						  depth + 1) ||
		     expression_references_symbol(type->type_expression, symbol,
						  depth + 1)))
			return true;
		type = type->ctype.base_type;
	}
	return false;
}

static bool statement_references_symbol(struct statement *s,
					struct symbol *symbol, unsigned int depth);

static bool expression_references_symbol(struct expression *e,
					 struct symbol *symbol, unsigned int depth)
{
	struct expression *item;
	struct type_expression *map;

	if (!e || depth >= 256)
		return false;
	switch (e->type) {
	case EXPR_SYMBOL:
		return e->symbol == symbol;
	case EXPR_TYPE:
		return type_references_symbol(e->symbol, symbol, depth + 1);
	case EXPR_PREOP:
	case EXPR_POSTOP:
		return expression_references_symbol(e->unop, symbol, depth + 1);
	case EXPR_BINOP:
	case EXPR_ASSIGNMENT:
	case EXPR_LOGICAL:
	case EXPR_COMMA:
	case EXPR_COMPARE:
		return expression_references_symbol(e->left, symbol, depth + 1) ||
		       expression_references_symbol(e->right, symbol, depth + 1);
	case EXPR_DEREF:
		return expression_references_symbol(e->deref, symbol, depth + 1);
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
	case EXPR_SIZEOF:
	case EXPR_ALIGNOF:
	case EXPR_PTRSIZEOF:
		return type_references_symbol(e->cast_type, symbol, depth + 1) ||
		       expression_references_symbol(e->cast_expression, symbol,
					    depth + 1);
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		return expression_references_symbol(e->conditional, symbol, depth + 1) ||
		       expression_references_symbol(e->cond_true, symbol, depth + 1) ||
		       expression_references_symbol(e->cond_false, symbol, depth + 1);
	case EXPR_STATEMENT:
		return statement_references_symbol(e->statement, symbol, depth + 1);
	case EXPR_CALL:
		if (expression_references_symbol(e->fn, symbol, depth + 1))
			return true;
		FOR_EACH_PTR(e->args, item) {
			if (expression_references_symbol(item, symbol, depth + 1))
				return true;
		} END_FOR_EACH_PTR(item);
		return false;
	case EXPR_INITIALIZER:
		FOR_EACH_PTR(e->expr_list, item) {
			if (expression_references_symbol(item, symbol, depth + 1))
				return true;
		} END_FOR_EACH_PTR(item);
		return false;
	case EXPR_IDENTIFIER:
		return expression_references_symbol(e->ident_expression, symbol,
					    depth + 1);
	case EXPR_INDEX:
		return expression_references_symbol(e->idx_expression, symbol,
					    depth + 1);
	case EXPR_POS:
		return expression_references_symbol(e->init_expr, symbol, depth + 1);
	case EXPR_SLICE:
		return expression_references_symbol(e->base, symbol, depth + 1);
	case EXPR_OFFSETOF:
		for (item = e; item; item = item->down) {
			if (item->op != '.' &&
			    expression_references_symbol(item->index, symbol, depth + 1))
				return true;
		}
		return false;
	case EXPR_GENERIC:
		if (expression_references_symbol(e->control, symbol, depth + 1) ||
		    expression_references_symbol(e->def, symbol, depth + 1))
			return true;
		for (map = e->map; map; map = map->next) {
			if (type_references_symbol(map->type, symbol, depth + 1) ||
			    expression_references_symbol(map->expr, symbol, depth + 1))
				return true;
		}
		return false;
	default:
		return false;
	}
}

static bool statement_references_symbol(struct statement *s,
					struct symbol *symbol, unsigned int depth)
{
	struct statement *child;
	struct expression *clobber;
	struct symbol *declaration;
	struct asm_operand *operand;

	if (!s || depth >= 256)
		return false;
	switch (s->type) {
	case STMT_COMPOUND:
		FOR_EACH_PTR(s->stmts, child) {
			if (statement_references_symbol(child, symbol, depth + 1))
				return true;
		} END_FOR_EACH_PTR(child);
		return false;
	case STMT_DECLARATION:
		FOR_EACH_PTR(s->declaration, declaration) {
			if (type_references_symbol(declaration, symbol, depth + 1) ||
			    expression_references_symbol(declaration->initializer, symbol,
						 depth + 1))
				return true;
		} END_FOR_EACH_PTR(declaration);
		return false;
	case STMT_EXPRESSION:
	case STMT_CONTEXT:
		return expression_references_symbol(s->expression, symbol, depth + 1) ||
		       expression_references_symbol(s->context, symbol, depth + 1);
	case STMT_RETURN:
		return expression_references_symbol(s->ret_value, symbol, depth + 1);
	case STMT_IF:
		return expression_references_symbol(s->if_conditional, symbol, depth + 1) ||
		       statement_references_symbol(s->if_true, symbol, depth + 1) ||
		       statement_references_symbol(s->if_false, symbol, depth + 1);
	case STMT_SWITCH:
		return expression_references_symbol(s->switch_expression, symbol,
					    depth + 1) ||
		       statement_references_symbol(s->switch_statement, symbol,
					   depth + 1);
	case STMT_CASE:
		return expression_references_symbol(s->case_expression, symbol,
					    depth + 1) ||
		       expression_references_symbol(s->case_to, symbol, depth + 1) ||
		       statement_references_symbol(s->case_statement, symbol, depth + 1);
	case STMT_ITERATOR:
		FOR_EACH_PTR(s->iterator_syms, declaration) {
			if (type_references_symbol(declaration, symbol, depth + 1) ||
			    expression_references_symbol(declaration->initializer, symbol,
						 depth + 1))
				return true;
		} END_FOR_EACH_PTR(declaration);
		return statement_references_symbol(s->iterator_pre_statement, symbol,
						   depth + 1) ||
		       expression_references_symbol(s->iterator_pre_condition, symbol,
						    depth + 1) ||
		       statement_references_symbol(s->iterator_statement, symbol,
						   depth + 1) ||
		       statement_references_symbol(s->iterator_post_statement, symbol,
						   depth + 1) ||
		       expression_references_symbol(s->iterator_post_condition, symbol,
						    depth + 1);
	case STMT_LABEL:
		return statement_references_symbol(s->label_statement, symbol,
					   depth + 1);
	case STMT_GOTO:
		return expression_references_symbol(s->goto_expression, symbol,
					    depth + 1);
	case STMT_ASM:
		if (expression_references_symbol(s->asm_string, symbol, depth + 1))
			return true;
		FOR_EACH_PTR(s->asm_outputs, operand) {
			if (expression_references_symbol(operand->expr, symbol, depth + 1))
				return true;
		} END_FOR_EACH_PTR(operand);
		FOR_EACH_PTR(s->asm_inputs, operand) {
			if (expression_references_symbol(operand->expr, symbol, depth + 1))
				return true;
		} END_FOR_EACH_PTR(operand);
		FOR_EACH_PTR(s->asm_clobbers, clobber) {
			if (expression_references_symbol(clobber, symbol, depth + 1))
				return true;
		} END_FOR_EACH_PTR(clobber);
		return false;
	case STMT_RANGE:
		return expression_references_symbol(s->range_expression, symbol,
					    depth + 1) ||
		       expression_references_symbol(s->range_low, symbol, depth + 1) ||
		       expression_references_symbol(s->range_high, symbol, depth + 1);
	default:
		return false;
	}
}

static bool has_type_expression(struct symbol *type)
{
	return type && type->type_expression &&
	       type->type_expression->type != EXPR_INITIALIZER;
}

static bool is_va_list_use(struct symbol *symbol)
{
	struct typedef_use *use;
	struct symbol *base_type;

	if (!symbol)
		return false;
	base_type = show_base_type(symbol);
	if (!base_type || base_type->type != SYM_PTR)
		return false;
	for (use = get_typedef_uses(); use; use = use->next) {
		if (use->pos.stream != symbol->pos.stream ||
		    use->pos.line != symbol->pos.line ||
		    use->pos.pos > symbol->pos.pos)
			continue;
		if (strcmp(use->name, "va_list") == 0)
			return true;
	}
	return false;
}

static struct symbol *show_base_type(struct symbol *type)
{
	if (type && type->orig_base_type)
		return type->orig_base_type;
	return type ? type->ctype.base_type : NULL;
}

static bool is_va_list_pointer(struct symbol *type)
{
	struct symbol *base_type = strip_type_wrappers(show_base_type(type));

	return !base_type || base_type->type != SYM_PTR;
}

static bool is_typeof(struct symbol *type)
{
	if (type && (type->type == SYM_TYPEOF ||
		     type->type == SYM_TYPEOF_UNQUAL))
		return true;
	/* hack around for evaluated code */
	return has_type_expression(type);
}

static bool type_has_typeof(struct symbol *type)
{
	struct symbol *arg;

	for (; type; type = show_base_type(type)) {
		if (is_typeof(type))
			return true;
		if (type->type != SYM_FN)
			continue;
		FOR_EACH_PTR(type->arguments, arg) {
			if (type_has_typeof(arg))
				return true;
		} END_FOR_EACH_PTR(arg);
	}
	return false;
}

static void show_typeof_expression(struct code_buffer *out,
				   struct symbol *type)
{
	if (type->type_expression)
		expr(out, type->type_expression);
	else
		append(out, "<empty type>");
}

/*
 * Based on show_c_typename(), but emit compilable GCC code rather than
 * Sparse checker types.  Keep source-preservation workarounds here for
 * evaluated types, including typeof(), __typeof_unqual__(), va_list, and
 * original base types.
 */
static void show_direct_c_typename(struct code_buffer *out, struct symbol *symbol,
					   struct c_type_name *name)
{
	struct symbol copy;
	struct symbol *type;
	struct ident *as = NULL;
	unsigned long modifiers = 0;
	unsigned long declaration_modifiers = 0;
	bool was_pointer = false;
	bool restricted = false;
	bool fouled = false;

	if (symbol->ctype.modifiers & MOD_REGISTER) {
		copy = *symbol;
		copy.ctype.modifiers &= ~MOD_EXTERN;
		symbol = &copy;
	}
	type = symbol;
	while (type) {
		const char *typename;

		if (type == &autotype_ctype) {
			modifiers &= ~MOD_SPECIFIER;
			if (name->start != name->end)
				type_name_prepend(name, " ");
			type_name_prepend(name, "__auto_type");
			goto out;
		}
		if (is_typeof(type)) {
			char expression[C_TYPE_NAME_BUFFER_SIZE];
			struct code_buffer expression_out = {
				.buf = expression,
				.len = sizeof(expression),
				.line_start = true,
			};
			const char *modifier_string;

			expression[0] = '\0';
			show_typeof_expression(&expression_out, type);
			if (name->start != name->end)
				type_name_prepend(name, " ");
			type_name_prepend_format(name, "%s(%s)",
				type->typeof_is_unqual ||
				type->type == SYM_TYPEOF_UNQUAL ?
				"__typeof_unqual__" : "typeof", expression);
			modifier_string = show_gcc_modifiers(modifiers);
			type_name_prepend(name, modifier_string);
			break;
		}
		if ((typename = builtin_typename(type))) {
			const char *modifier_string;

			modifiers &= ~MOD_SPECIFIER;
			if (name->start != name->end)
				type_name_prepend(name, " ");
			type_name_prepend(name, typename);
			modifier_string = show_gcc_modifiers(modifiers);
			type_name_prepend(name, modifier_string);
			break;
		}

		switch (type->type) {
		case SYM_PTR: {
			unsigned long pointer_modifiers =
				modifiers & (MOD_QUALIFIER | MOD_ATOMIC);

			if (is_va_list_use(symbol) &&
			    is_va_list_pointer(type)) {
				type_name_prepend(name, "va_list ");
				goto out;
			}
			if (pointer_modifiers) {
				type_name_prepend_format(name, "*%s",
					show_gcc_modifiers(pointer_modifiers));
			} else {
				type_name_prepend(name, "*");
			}
			modifiers &= ~pointer_modifiers;
			modifiers |= type->ctype.modifiers;
			as = type->ctype.as;
			was_pointer = true;
			examine_pointer_target(type);
			break;
		}
		case SYM_FN: {
			struct symbol *arg;
			bool comma = false;

			if (was_pointer) {
				type_name_prepend(name, "(");
				type_name_append(name, ")");
				was_pointer = false;
			}
			type_name_append(name, "(");
			FOR_EACH_PTR(type->arguments, arg) {
				char array[C_TYPE_NAME_BUFFER_SIZE];
				struct c_type_name arg_name = {
					.start = array + C_TYPE_NAME_BUFFER_MIDDLE,
					.end = array + C_TYPE_NAME_BUFFER_MIDDLE,
				};

				show_direct_c_typename(out, arg, &arg_name);
				*arg_name.end = '\0';
				type_name_append_format(name, "%s%s", comma ? ", " : "",
						arg_name.start);
				comma = true;
			} END_FOR_EACH_PTR(arg);
			if (!comma && !type->variadic)
				type_name_append(name, "void");
			if (type->variadic)
				type_name_append_format(name, "%s...", comma ? ", " : "");
			type_name_append(name, ")");
			break;
		}
		case SYM_STRUCT:
		case SYM_UNION:
		case SYM_ENUM:
			show_direct_tag_typename(name, type, modifiers);
			goto out;
		case SYM_NODE:
			if (type->ident)
				type_name_append_format(name, "%s", show_ident(type->ident));
			declaration_modifiers |= type->ctype.modifiers & MOD_DECLARE;
			modifiers |= type->ctype.modifiers & ~MOD_DECLARE;
			combine_address_space(type->pos, &as, type->ctype.as);
			break;
		case SYM_BITFIELD:
			modifiers |= type->ctype.modifiers;
			combine_address_space(type->pos, &as, type->ctype.as);
			type_name_append_format(name, ":%d", type->bit_size);
			break;
		case SYM_ARRAY:
			modifiers |= type->ctype.modifiers;
			combine_address_space(type->pos, &as, type->ctype.as);
			if (was_pointer) {
				type_name_prepend(name, "(");
				type_name_append(name, ")");
				was_pointer = false;
			}
			if (type->array_size)
				type_name_append_format(name, "[%lld]",
						get_expression_value(type->array_size));
			else
				type_name_append(name, "[]");
			break;
		case SYM_RESTRICT:
			if (!type->ident) {
				restricted = true;
				break;
			}
			if (name->start != name->end)
				type_name_prepend(name, " ");
			type_name_prepend_format(name, "%s", show_ident(type->ident));
			type_name_prepend(name, show_gcc_modifiers(modifiers &
				(MOD_QUALIFIER | MOD_ATOMIC)));
			goto out;
		case SYM_FOULED:
			fouled = true;
			break;
		default:
			if (name->start != name->end)
				type_name_prepend(name, " ");
			type_name_prepend_format(name, "unknown type %d", type->type);
			goto out;
		}
		type = show_base_type(type);
	}
out:
	if (declaration_modifiers)
		type_name_prepend(name, show_gcc_modifiers(declaration_modifiers));
	if (restricted)
		type_name_prepend(name, "restrict ");
	if (fouled)
		type_name_prepend(name, "fouled ");
	if (name->end > name->start && name->end[-1] == ' ')
		name->end--;
}

static void show_c_typename(struct code_buffer *out, struct symbol *s)
{
	char array[C_TYPE_NAME_BUFFER_SIZE];
	struct c_type_name name = {
		.start = array + C_TYPE_NAME_BUFFER_MIDDLE,
		.end = array + C_TYPE_NAME_BUFFER_MIDDLE,
	};

	show_direct_c_typename(out, s, &name);
	*name.end = '\0';
	append(out, "%s", name.start);
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

static bool has_bitwise_parent(struct expression *e)
{
	struct expression *parent;

	parent = e->parent & 0x1UL ?
		(struct expression *)(e->parent & ~0x1UL) : NULL;
	for (; parent; parent = parent->parent & 0x1UL ?
		(struct expression *)(parent->parent & ~0x1UL) : NULL) {
		if ((parent->type == EXPR_BINOP || parent->type == EXPR_COMPARE ||
		     parent->type == EXPR_ASSIGNMENT) &&
		    (parent->op == '|' || parent->op == '&' || parent->op == '^' ||
		     parent->op == SPECIAL_EQUAL || parent->op == SPECIAL_NOTEQUAL ||
		     parent->op == SPECIAL_AND_ASSIGN || parent->op == SPECIAL_OR_ASSIGN ||
		     parent->op == SPECIAL_XOR_ASSIGN || parent->op == SPECIAL_LEFTSHIFT ||
		     parent->op == SPECIAL_RIGHTSHIFT || parent->op == SPECIAL_SHL_ASSIGN ||
		     parent->op == SPECIAL_SHR_ASSIGN))
			return true;
	}
	return false;
}

static const char *integer_literal_suffix(struct expression *e)
{
	struct symbol *type = strip_type_wrappers(e->ctype);

	if (type == &uint_ctype)
		return "U";
	if (type == &long_ctype)
		return "L";
	if (type == &ulong_ctype)
		return "UL";
	if (type == &llong_ctype)
		return "LL";
	if (type == &ullong_ctype)
		return "ULL";
	return "";
}

static long long sign_extend_value(struct expression *e)
{
	struct symbol *type = strip_type_wrappers(e->ctype);
	unsigned long long value = e->value;
	unsigned int bits;

	if (!type)
		return value;
	bits = type->bit_size;
	if (bits == 0 || bits >= 64)
		return value;
	if (value & (1ULL << (bits - 1)))
		value |= ~((1ULL << bits) - 1);
	return value;
}

static void show_value(struct code_buffer *out, struct expression *e)
{
	long long signed_value = sign_extend_value(e);
	unsigned long long value = e->value;
	const char *suffix = integer_literal_suffix(e);

	if (!e->ctype || e->ctype != &int_ctype)
		goto hex;
	if (has_bitwise_parent(e))
		goto hex;
	if (is_signed_type(e->ctype) &&
	    signed_value >= -4059 && signed_value < 0) {
		append(out, "%lld%s", signed_value, suffix);
		return;
	}
	if (value > 16) {
		unsigned long long bits = value;

		while (!(bits & 1))
			bits >>= 1;
		if (!(bits & (bits + 1)))
			goto hex;
	}
	append(out, "%llu%s", value, suffix);
	return;
hex:
	append(out, "0x%llx%s", value, suffix);
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

	if (is_typeof(e->symbol)) {
		append(out, "%s(", e->symbol->typeof_is_unqual ||
		       e->symbol->type == SYM_TYPEOF_UNQUAL ?
		       "__typeof_unqual__" : "typeof");
		show_typeof_expression(out, e->symbol);
		append(out, ")");
		return;
	}
	if (is_typeof(type)) {
		append(out, type->typeof_is_unqual ||
		       type->type == SYM_TYPEOF_UNQUAL ? "__typeof_unqual__(" :
		       "typeof(");
		show_typeof_expression(out, type);
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
		if (!part->op)
			break;
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
	struct expression *previous;

	if (!e) { append(out, "/* null */"); return; }
	previous = rendering_expression;
	if (!e->parent && previous)
		e->parent = (unsigned long)previous | 0x1UL;
	rendering_expression = e;
	switch (e->type) {
	case EXPR_VALUE: show_value(out, e); break;
	case EXPR_FVALUE: show_fvalue(out, e); break;
	case EXPR_STRING:
		append(out, "%s%s", e->wide ? "L" : "", show_string(e->string));
		break;
	case EXPR_TYPE:
		show_type_expression(out, e);
		break;
	case EXPR_SYMBOL:
		if (e->symbol && e->symbol->ident)
			append(out, "%s", show_ident(e->symbol->ident));
		else if (e->symbol_name)
			append(out, "%s", show_ident(e->symbol_name));
		else if (e->symbol && e->symbol->initializer)
			expr(out, e->symbol->initializer);
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
	case EXPR_POSTOP:
		append(out, "(");
		expr(out, e->unop);
		append(out, "%s)", show_special(e->op));
		break;
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
	case EXPR_BINOP:
	case EXPR_LOGICAL:
	case EXPR_ASSIGNMENT:
	case EXPR_COMMA:
		append(out, "(");
		expr(out, e->left);
		append(out, " %s ", show_special(e->op));
		expr(out, e->right);
		append(out, ")");
		break;
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
		append(out, "(");
		exprs(out, e->args);
		append(out, ")");
		break;
	case EXPR_CAST: case EXPR_FORCE_CAST:
		append(out, "((");
		show_c_typename(out, e->cast_type);
		append(out, ")");
		expr(out, e->cast_expression);
		append(out, ")");
		break;
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
		append(out, "(");
		expr(out, e->conditional);
		append(out, " ? ");
		expr(out, e->cond_true);
		append(out, " : ");
		expr(out, e->cond_false);
		append(out, ")");
		break;
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
	default:
		append(out, "/* unsupported expression %d */", e->type);
		break;
	}
	rendering_expression = previous;
}

static void show_anonymous_tag(struct code_buffer *out, struct symbol *type);

static void declaration(struct code_buffer *out, struct symbol *s)
{
	struct symbol *anonymous_tag = type_has_typeof(s) ? NULL :
		find_anonymous_tag(s);
	struct symbol *tag = s;
	unsigned int i;

	while (tag) {
		if (is_tag_type(tag) &&
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

	if (anonymous_tag)
		show_anonymous_c_typename(out, s, anonymous_tag);
	else
		show_c_typename(out, s);

	show_asm_name(out, s);
	if (symbol_initializer(s)) {
		append(out, " = ");
		expr(out, symbol_initializer(s));
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

	if (!s) {
		append(out, "{}\n");
		return;
	}
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
	case STMT_DECLARATION:
		declarations(out, s->declaration);
		break;
	case STMT_EXPRESSION:
		expr(out, s->expression);
		append(out, ";\n");
		break;
	case STMT_RETURN:
		append(out, "return");
		if (s->ret_value) {
			append(out, " ");
			expr(out, s->ret_value);
		}
		append(out, ";\n");
		break;
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
		append(out, "if (");
		expr(out, s->if_conditional);
		append(out, ") ");
		stmt(out, s->if_true);
		if (s->if_false) {
			append(out, "else ");
			stmt(out, s->if_false);
		}
		break;
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
	default:
		append(out, "/* unsupported statement %d */\n", s->type);
		break;
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

static bool statement_has_asm_goto(struct statement *s)
{
	struct statement *child;

	if (!s)
		return false;
	if (s->type == STMT_ASM && (s->asm_modifiers & MOD_ASM_GOTO))
		return true;
	if (s->type != STMT_COMPOUND)
		return false;
	FOR_EACH_PTR(s->stmts, child) {
		if (statement_has_asm_goto(child))
			return true;
	} END_FOR_EACH_PTR(child);
	return false;
}

static void sym(struct code_buffer *out, struct symbol *s)
{
	struct symbol *type;

	if (!s) {
		append(out, "/* null symbol */");
		return;
	}

	if (is_type_definition(s, &type))
		return;
	if (s->namespace == NS_TYPEDEF && typedef_was_output(s))
		return;
	if (symbol_was_output(printed.data_definitions, s))
		return;

	type = s->ctype.base_type;
	if (type && type->type == SYM_FN) {
		show_function(out, s);
		return;
	}

	show_initializer_dependencies(out, s);
	declaration(out, s);
	if (!(s->ctype.modifiers & MOD_EXTERN))
		remember_symbol(&printed.data_definitions, s);
}

static struct symbol *find_anonymous_tag(struct symbol *type)
{
	while (type) {
		if (is_tag_type(type) && !type->ident)
			return type;
		type = show_base_type(type);
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

static void show_function(struct code_buffer *out, struct symbol *s)
{
	struct symbol *type = s->ctype.base_type;
	struct statement *body;
	bool has_definition, asm_goto, external_inline;

	if (!type)
		return;
	body = type->stmt ? type->stmt : type->inline_stmt;
	has_definition = body != NULL;
	asm_goto = statement_has_asm_goto(body);
	external_inline = has_definition &&
		(s->ctype.modifiers & MOD_INLINE) &&
		!(s->ctype.modifiers & MOD_STATIC);
	if (has_definition &&
	    symbol_was_output(printed.function_definitions, s))
		return;
	if (!has_definition &&
	    (symbol_was_output(printed.function_definitions, s) ||
	     symbol_was_output(printed.function_forwards, s)))
		return;

	show_function_forwards(out, type);
	show_function_dependencies(out, s);
	if (has_definition) {
		if (external_inline) {
			struct symbol copy = *s;

			copy.ctype.modifiers &= ~(MOD_INLINE | MOD_GNU_INLINE);
			copy.ctype.modifiers |= MOD_EXTERN;
			show_c_typename(out, &copy);
			append(out, ";\n");
		}
		if (asm_goto || external_inline) {
			struct symbol copy = *s;

			if (asm_goto)
				copy.ctype.modifiers |= MOD_ALWAYS_INLINE;
			if (external_inline)
				copy.ctype.modifiers &= ~MOD_EXTERN;
			show_c_typename(out, &copy);
		} else {
			show_c_typename(out, s);
		}
		append(out, "\n");
		function_body(out, body);
		remember_symbol(&printed.function_definitions, s);
	} else {
		show_c_typename(out, s);
		append(out, ";\n");
		remember_symbol(&printed.function_forwards, s);
	}
}

struct inline_function_output {
	struct code_buffer *out;
	struct symbol_list **emitted;
	struct symbol *active[8192];
	struct symbol *data[8192];
	unsigned int active_count;
	unsigned int data_count;
};

static bool symbol_in_array(struct symbol **symbols, unsigned int count,
			    struct symbol *symbol)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (symbols[i] == symbol)
			return true;
	return false;
}

static struct symbol *find_inline_function_definition(struct symbol *function)
{
	unsigned int i;

	for (i = 0; function && i < 8192; i++) {
		struct symbol *type = strip_type_wrappers(function);

		if (type && type->type == SYM_FN && type->inline_stmt)
			return function;
		function = function->same_symbol;
	}
	return NULL;
}

static void show_inline_function_definition(struct inline_function_output *output,
					    struct symbol *function);

static void show_inline_function_declaration(struct inline_function_output *output,
					     struct symbol *function)
{
	if (symbol_was_output(printed.function_definitions, function) ||
	    symbol_was_output(printed.function_forwards, function))
		return;
	show_function_forwards(output->out, function->ctype.base_type);
	show_c_typename(output->out, function);
	append(output->out, ";\n");
	remember_symbol(&printed.function_forwards, function);
}

static void show_inline_data_dependency(struct inline_function_output *output,
						struct symbol *symbol)
{
	struct symbol *type = strip_type_wrappers(symbol);
	struct symbol copy;
	struct symbol_dependencies dependencies = { };
	struct tag_output type_output = { .out = output->out };
	struct symbol *dependency;
	unsigned int i;

	if (!type || type->type == SYM_FN ||
	    symbol_in_array(output->data, output->data_count, symbol))
		return;
	if (output->data_count >= ARRAY_SIZE(output->data))
		return;
	output->data[output->data_count++] = symbol;
	show_type_dependencies(&type_output, symbol,
			       type_can_use_forward_declaration(symbol));
	collect_type_expression_dependencies(&dependencies, symbol);
	for (i = 0; i < dependencies.symbol_count; i++) {
		dependency = dependencies.symbols[i];
		if (dependency == symbol)
			dependency = dependency->same_symbol;
		if (dependency)
			show_inline_data_dependency(output, dependency);
	}
	if (symbol->ctype.modifiers & MOD_STATIC)
		sym(output->out, symbol);
	else {
		copy = *symbol;
		copy.ctype.modifiers |= MOD_EXTERN;
		show_c_typename(output->out, &copy);
		show_asm_name(output->out, &copy);
		append(output->out, ";\n");
	}
	if (output->emitted)
		add_symbol(output->emitted, symbol);
}

static void show_inline_function_dependencies(struct inline_function_output *output,
					      struct symbol *function)
{
	struct symbol_dependencies dependencies = { };
	struct tag_output type_output = { .out = output->out };
	struct symbol *dependency;
	unsigned int i;

	collect_function_dependencies(&dependencies, function);
	for (i = 0; i < dependencies.types.count; i++)
		show_required_type(&type_output, dependencies.types.types[i],
				   dependencies.types.needs_forward[i]);
	for (i = 0; i < dependencies.symbol_count; i++) {
		struct symbol *inline_function;

		dependency = dependencies.symbols[i];
		if (dependency == function)
			continue;
		access_symbol(dependency);
		inline_function = find_inline_function_definition(dependency);
		if (inline_function)
			show_inline_function_definition(output, inline_function);
		else
			show_inline_data_dependency(output, dependency);
	}
}

static void show_inline_function_definition(struct inline_function_output *output,
					    struct symbol *function)
{
	struct symbol *type = strip_type_wrappers(function);

	if (!type || type->type != SYM_FN || !type->inline_stmt ||
	    symbol_was_output(printed.function_definitions, function))
		return;
	if (symbol_in_array(output->active, output->active_count, function)) {
		show_inline_function_declaration(output, function);
		return;
	}
	if (output->active_count >= ARRAY_SIZE(output->active))
		return;
	output->active[output->active_count++] = function;
	show_inline_function_dependencies(output, function);
	output->active_count--;
	show_function(output->out, function);
}

static bool show_inline_function_for_symbol(struct code_buffer *out,
					    struct symbol *function)
{
	struct inline_function_output output = { .out = out };
	struct symbol *inline_function;

	inline_function = find_inline_function_definition(function);
	if (!inline_function)
		return false;
	show_inline_function_definition(&output, inline_function);
	return true;
}

void show_inline_function_definitions(struct symbol_list *symbols,
					      struct symbol_list **emitted, char *buf, size_t len)
{
	struct code_buffer out = { .buf = buf, .len = len, .line_start = true };
	struct inline_function_output output = { .out = &out, .emitted = emitted };
	struct symbol *symbol;

	if (!len)
		return;
	buf[0] = '\0';
	FOR_EACH_PTR(symbols, symbol) {
		struct symbol *type = strip_type_wrappers(symbol);

		if (type && type->type == SYM_FN)
			show_inline_function_dependencies(&output, symbol);
		show_inline_function_definition(&output, symbol);
	} END_FOR_EACH_PTR(symbol);
}

static void show_struct_definition(struct tag_output *output, struct symbol *type)
{
	struct code_buffer *out = output->out;
	struct symbol *member;
	struct symbol *anonymous_tag;

	show_type_members(output, type);
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
	if (is_typedef_tag(type)) {
		append(out, "} %s", show_ident(type->ident));
		if (type->transparent_union)
			append(out, " __attribute__((__transparent_union__))");
		append(out, ";\n");
	} else
		append(out, "};\n");
	remember_tag(&printed.type_definitions, type);
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
	remember_tag(&printed.type_definitions, type);
}

static void show_tag_definition(struct tag_output *output, struct symbol *type)
{
	struct symbol *typedef_symbol;

	if (is_typedef_tag(type)) {
		typedef_symbol = lookup_symbol(type->ident, NS_TYPEDEF);
		if (typedef_symbol &&
		    tag_was_output(printed.typedefs, typedef_symbol))
			return;
		if (typedef_symbol)
			remember_tag(&printed.typedefs, typedef_symbol);
	}
	if (type->type == SYM_ENUM)
		show_enum_definition(output, type);
	else
		show_struct_definition(output, type);
}

static void show_typedef_definition(struct code_buffer *out, struct symbol *type)
{
	struct symbol copy;
	struct symbol *base_type;

	if (type->ident && strcmp(show_ident(type->ident), "va_list") == 0) {
		append(out, "typedef __builtin_va_list va_list;\n");
		return;
	}
	append(out, "typedef ");
	base_type = type->ctype.base_type;
	if (base_type && base_type->type == SYM_RESTRICT &&
	    base_type->ident == type->ident) {
		copy = *type;
		copy.ctype.base_type = base_type->ctype.base_type;
		type = &copy;
	}
	show_c_typename(out, type);
	append(out, ";\n");
}

static void show_type_definition(struct tag_output *output,
				 struct symbol *symbol)
{
	struct symbol *tag;

	if (is_type_definition(symbol, &tag)) {
		show_type_members(output, tag);
		if (!tag_was_output(printed.type_definitions, tag))
			show_tag_definition(output, tag);
		return;
	}
	if (symbol->namespace != NS_TYPEDEF || !symbol->ident ||
	    lookup_symbol(symbol->ident, NS_TYPEDEF) != symbol)
		return;
	if (tag_was_output(printed.typedefs, symbol))
		return;
	remember_tag(&printed.typedefs, symbol);
	show_type_dependencies(output, symbol->ctype.base_type,
			       true);
	show_typedef_definition(output->out, symbol);
}

void show_all_type_definitions(struct symbol_list *symbols, char *buf,
				       size_t len)
{
	struct code_buffer out = { .buf = buf, .len = len, .line_start = true };
	struct tag_output output = { .out = &out };
	struct symbol *symbol;

	if (!len)
		return;
	buf[0] = '\0';
	printed = (struct output_dependencies) { };

	FOR_EACH_PTR(symbols, symbol) {
		show_type_definition(&output, symbol);
	} END_FOR_EACH_PTR(symbol);
}

void show_expr(struct expression *e, char *buf, size_t len)
{
	struct code_buffer out = { .buf = buf, .len = len, .line_start = true };

	if (!len)
		return;
	buf[0] = '\0';
	expr(&out, e);
}

void show_stmt(struct statement *s, char *buf, size_t len)
{
	struct code_buffer out = { .buf = buf, .len = len, .line_start = true };

	if (!len)
		return;
	buf[0] = '\0';
	stmt(&out, s);
}

void show_sym(struct symbol *s, char *buf, size_t len)
{
	struct code_buffer out = { .buf = buf, .len = len, .line_start = true };

	if (!len)
		return;
	buf[0] = '\0';
	sym(&out, s);
}
