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
	/* The buffer is malloc()ed and can be reallocated when it is full. */
	bool grow;
	struct symbol_list *local_tags;
	struct symbol_list *printing;
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

	if (out->grow) {
		size_t needed = out->used + ret + 1;

		/* Leave room for the indent of every line. */
		for (i = 0; i < ret; i++) {
			if (buffer[i] == '\n')
				needed += out->indent;
		}
		needed += out->indent;
		if (needed > out->len) {
			char *buf = realloc(out->buf, needed * 2);

			if (!buf)
				die("out of memory");
			out->buf = buf;
			out->len = needed * 2;
		}
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
static void show_symbol_declaration(struct code_buffer *out, struct symbol *s);
static struct symbol *function_definition(struct symbol *s);
static bool declaration_has_typeof(struct symbol *type);
static bool is_incomplete_array_declaration(struct symbol *symbol);
static bool is_typedef_tag(struct symbol *type);
static struct symbol *find_anonymous_tag(struct symbol *type);
static void show_anonymous_c_typename(struct code_buffer *out,
				      struct symbol *type,
				      struct symbol *anonymous_tag);
static void show_anonymous_declarator(struct code_buffer *out,
				      struct symbol *type,
				      struct symbol *anonymous_tag);
static void show_c_typename(struct code_buffer *out, struct symbol *s);
static void show_asm_name(struct code_buffer *out, struct symbol *s);
static void show_initializer_dependencies(struct code_buffer *out,
					  struct symbol *symbol);
static void show_initializer_dependencies_rec(struct code_buffer *out,
					      struct symbol *symbol,
					      struct symbol_list **active);
static bool is_type_definition(struct symbol *s, struct symbol **tag_out);
static bool tag_has_definition(struct symbol *tag);
static bool is_struct_or_union(struct symbol *type);
static bool is_tag_type(struct symbol *type);
static const char *tag_type_name(struct symbol *type);
static bool has_type_expression(struct symbol *type);
static bool is_va_list_use(struct symbol *symbol);
static struct symbol *show_base_type(struct symbol *type);
struct sym_dependency;
struct sym_dependency_list;
static void collect_statement_dependencies(struct sym_dependency *dependency,
					   struct statement *s);
static struct symbol *collect_statement_result_dependencies(struct sym_dependency *dependency,
							   struct statement *s,
							   bool complete_type);
static void collect_type_expression_dependencies(struct sym_dependency *dependency,
						 struct symbol *type);
static void show_dependencies(struct code_buffer *out,
			      struct sym_dependency *parent);

/*
 * There is one shared node for each symbol and decl_enough value so a
 * symbol's dependencies are only collected once.  The roots are not shared.
 */
struct sym_dependency {
	struct symbol *sym;
	struct sym_dependency_list *depends;
	/* Define it if possible but a declaration is enough. */
	bool decl_enough;
	bool root;
	bool collected;
	bool shown;
};
DECLARE_PTR_LIST(sym_dependency_list, struct sym_dependency);

#define DEPENDENCY_HASH_SIZE 4096
static struct sym_dependency_list *dependency_hash[DEPENDENCY_HASH_SIZE];
/* The ancestors of the dependency which is being printed. */
static struct symbol_list *showing;
static struct symbol *collect_expression_dependencies_rec(struct sym_dependency *dependency,
							  struct expression *e,
							  bool complete_type);

struct typeof_type {
	struct symbol *typeof_sym;
	struct symbol *type;
};
DECLARE_PTR_LIST(typeof_type_list, struct typeof_type);
static struct typeof_type_list *typeof_types;

/* A type name is built outwards from the declarator, so it grows both ways. */
struct c_type_name {
	char *buf;
	size_t size;
	char *start;
	char *end;
};

#define C_TYPE_NAME_BUFFER_SIZE 16384

/* Dependency collection and emission. */

static struct expression *find_symbol_initializer(struct symbol *symbol)
{
	do {
		if (symbol->orig_initializer)
			return symbol->orig_initializer;
		if (symbol->initializer)
			return symbol->initializer;
	} while ((symbol = symbol->same_symbol) != NULL);
	return NULL;
}

static struct expression *symbol_initializer(struct symbol *symbol)
{
	return find_symbol_initializer(symbol);
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
	switch (type->type) {
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

static void collect_dependencies(struct sym_dependency *dependency,
				 struct symbol *type, bool decl_enough);
static void add_type_dependency(struct sym_dependency *dependency,
				struct symbol *type, bool decl_enough);

/*
 * Examining a typeof() evaluates the expression and changes the symbol to a
 * SYM_NODE in place, so a later output file would see different types.
 * Work out the type from the unevaluated expression instead.
 */
static struct symbol *typeof_base_type(struct symbol *type)
{
	struct sym_dependency root = { .sym = type, .root = true };
	struct typeof_type *entry;

	FOR_EACH_PTR(typeof_types, entry) {
		if (entry->typeof_sym == type)
			return entry->type;
	} END_FOR_EACH_PTR(entry);
	entry = malloc(sizeof(*entry));
	if (!entry)
		return NULL;
	entry->typeof_sym = type;
	entry->type = NULL;
	add_ptr_list(&typeof_types, entry);
	entry->type = collect_expression_dependencies_rec(&root, type->initializer,
							  false);
	return entry->type;
}

static struct symbol *strip_type_wrappers(struct symbol *type)
{
	while (type && (type->type == SYM_NODE || type->type == SYM_RESTRICT ||
			type->type == SYM_TYPEOF ||
			type->type == SYM_TYPEOF_UNQUAL)) {
		if (type->type == SYM_NODE || type->type == SYM_RESTRICT)
			type = show_base_type(type);
		else
			type = typeof_base_type(type);
	}
	return type;
}

static struct symbol *strip_bitfield_type(struct symbol *type)
{
	type = strip_type_wrappers(type);
	while (type && type->type == SYM_BITFIELD)
		type = strip_type_wrappers(show_base_type(type));
	return type;
}

static struct symbol *type_typedef_symbol(struct symbol *type)
{
	struct symbol *typedef_symbol;

	/* The base type of a __bitwise enum is not a typedef for the enum. */
	if (!type || is_tag_type(type))
		return NULL;
	if (type->type == SYM_RESTRICT && type->ident) {
		typedef_symbol = lookup_symbol(type->ident, NS_TYPEDEF);
		if (typedef_symbol && typedef_symbol != type)
			return typedef_symbol;
	}
	for (type = show_base_type(type); type &&
	     (type->type == SYM_NODE || type->type == SYM_RESTRICT ||
	      type->type == SYM_BITFIELD); type = show_base_type(type)) {
		if ((type->type == SYM_NODE || type->type == SYM_RESTRICT) &&
		    type->ident) {
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
	if (type && type->namespace == NS_SYMBOL &&
	    (type->ctype.modifiers & MOD_EXTERN)) {
		type = show_base_type(type);
		type = strip_type_wrappers(type);
		while (type_is_array(type))
			type = strip_type_wrappers(show_base_type(type));
		if (is_struct_or_union(type))
			return true;
	}
	type = strip_type_wrappers(type);
	while (type_is_array(type))
		type = strip_type_wrappers(show_base_type(type));
	if (!type_is_ptr(type))
		return false;
	type = strip_type_wrappers(show_base_type(type));
	/* "struct foo (*p)[]" needs the array elements to be complete. */
	if (type_is_array(type)) {
		while (type_is_array(type))
			type = strip_type_wrappers(show_base_type(type));
		return type_is_ptr(type);
	}
	return !type || type->type != SYM_ENUM;
}

static struct sym_dependency *find_dependency(struct sym_dependency *parent,
					      struct symbol *symbol)
{
	struct sym_dependency *dependency;

	FOR_EACH_PTR(parent->depends, dependency) {
		if (dependency->sym == symbol)
			return dependency;
	} END_FOR_EACH_PTR(dependency);
	return NULL;
}

static struct sym_dependency *shared_dependency(struct symbol *symbol,
						bool decl_enough)
{
	struct sym_dependency_list **bucket;
	struct sym_dependency *dependency;

	bucket = &dependency_hash[((unsigned long)symbol / sizeof(void *) * 2 +
				   decl_enough) % DEPENDENCY_HASH_SIZE];
	FOR_EACH_PTR(*bucket, dependency) {
		if (dependency->sym == symbol &&
		    dependency->decl_enough == decl_enough)
			return dependency;
	} END_FOR_EACH_PTR(dependency);
	dependency = calloc(1, sizeof(*dependency));
	if (!dependency)
		return NULL;
	dependency->sym = symbol;
	dependency->decl_enough = decl_enough;
	add_ptr_list(bucket, dependency);
	return dependency;
}

static void free_dependencies(void)
{
	struct sym_dependency *dependency;
	int i;

	for (i = 0; i < DEPENDENCY_HASH_SIZE; i++) {
		FOR_EACH_PTR(dependency_hash[i], dependency) {
			free_ptr_list(&dependency->depends);
			free(dependency);
		} END_FOR_EACH_PTR(dependency);
		free_ptr_list(&dependency_hash[i]);
	}
}

static struct sym_dependency *add_dependency(struct sym_dependency *parent,
					     struct symbol *symbol, bool decl_enough)
{
	struct sym_dependency *dependency;
	struct sym_dependency *needed;

	if (!parent || !symbol)
		return NULL;
	dependency = find_dependency(parent, symbol);
	if (dependency) {
		if (dependency->decl_enough && !decl_enough) {
			needed = shared_dependency(symbol, false);
			if (!needed)
				return dependency;
			FOR_EACH_PTR(parent->depends, dependency) {
				if (dependency->sym == symbol)
					REPLACE_CURRENT_PTR(dependency, needed);
			} END_FOR_EACH_PTR(dependency);
			return needed;
		}
		return dependency;
	}
	dependency = shared_dependency(symbol, decl_enough);
	if (!dependency)
		return NULL;
	add_ptr_list(&parent->depends, dependency);
	return dependency;
}

static struct symbol *tag_type(struct symbol *type)
{

	type = strip_type_wrappers(type);
	while (type_is_ptr(type) || type_is_array(type))
		type = strip_type_wrappers(show_base_type(type));
	if (type && type->definition && is_tag_type(type->definition))
		type = type->definition;
	if (!is_tag_type(type))
		return NULL;
	return type;
}

static struct symbol *tag_dependency_symbol(struct symbol *type)
{
	type = tag_type(type);
	if (!type || !type->ident)
		return NULL;
	return type;
}

static void collect_symbol_dependencies(struct sym_dependency *dependency);

static bool dependency_has_ancestor(struct sym_dependency *dependency)
{
	return lookup_ptr_list_entry((struct ptr_list *)showing,
				     dependency->sym);
}

static struct sym_dependency *add_tag_dependency(struct sym_dependency *parent,
						  struct symbol *type,
						  bool decl_enough)
{
	struct sym_dependency *dependency;

	type = tag_type(type);
	if (!type || type == parent->sym)
		return NULL;
	dependency = add_dependency(parent, type, decl_enough);
	return dependency;
}

static void collect_dependencies(struct sym_dependency *parent,
				 struct symbol *type, bool decl_enough)
{
	struct symbol *struct_type;
	struct symbol *member;
	struct symbol *arg;
	struct symbol *typedef_symbol;
	struct sym_dependency *dependency;

	if (!type)
		return;
	if (is_va_list_use(type)) {
		typedef_symbol = lookup_symbol(built_in_ident("va_list"),
					      NS_TYPEDEF);
		dependency = add_dependency(parent, typedef_symbol, decl_enough);
		if (dependency)
			collect_symbol_dependencies(dependency);
	}
	if (type->scope && !toplevel(type->scope)) {
		if (is_tag_type(type)) {
			FOR_EACH_PTR(type->symbol_list, member) {
				collect_dependencies(parent, member,
						     type_can_use_forward_declaration(member));
			} END_FOR_EACH_PTR(member);
		}
		return;
	}
	switch (type->type) {
	case SYM_NODE:
	case SYM_RESTRICT:
	case SYM_BITFIELD:
		typedef_symbol = type_typedef_symbol(type);
		if (typedef_symbol && typedef_symbol != type) {
			dependency = add_dependency(parent, typedef_symbol,
						    decl_enough);
			if (dependency)
				collect_symbol_dependencies(dependency);
			return;
		}
		collect_dependencies(parent, show_base_type(type), decl_enough);
		return;
	case SYM_TYPEOF:
	case SYM_TYPEOF_UNQUAL:
		dependency = add_dependency(parent, type, decl_enough);
		if (dependency)
			collect_symbol_dependencies(dependency);
		return;
	case SYM_PTR:
	case SYM_ARRAY:
		collect_dependencies(parent, show_base_type(type),
				     type_can_use_forward_declaration(type));
		return;
	case SYM_FN:
		add_type_dependency(parent, type->ctype.base_type,
				    type_can_use_forward_declaration(
					    type->ctype.base_type));
		FOR_EACH_PTR(type->arguments, arg) {
			add_type_dependency(parent, arg,
					    type_can_use_forward_declaration(arg));
			collect_type_expression_dependencies(parent, arg);
		} END_FOR_EACH_PTR(arg);
		return;
	case SYM_ENUM:
		add_tag_dependency(parent, type, decl_enough);
		return;
	case SYM_STRUCT:
	case SYM_UNION:
		dependency = add_tag_dependency(parent, type, decl_enough);
		if (!dependency || decl_enough || dependency->collected)
			return;
		dependency->collected = true;
		struct_type = dependency->sym;
		if (!struct_type->symbol_list)
			return;
		FOR_EACH_PTR(struct_type->symbol_list, member) {
			collect_dependencies(dependency, member,
					     type_can_use_forward_declaration(member));
		} END_FOR_EACH_PTR(member);
		return;
	default:
		return;
	}
}

static void collect_function_type_dependencies(struct sym_dependency *parent,
					       struct symbol *type)
{
	collect_dependencies(parent, type,
			     type_can_use_forward_declaration(type));
}

static void add_type_dependency(struct sym_dependency *parent,
				struct symbol *type, bool decl_enough)
{
	struct symbol *typedef_symbol;
	struct sym_dependency *dependency;

	if (!type)
		return;
	if (is_va_list_use(type)) {
		typedef_symbol = lookup_symbol(built_in_ident("va_list"),
					      NS_TYPEDEF);
		dependency = add_dependency(parent, typedef_symbol, decl_enough);
		if (dependency)
			collect_symbol_dependencies(dependency);
	}
	typedef_symbol = type_typedef_symbol(type);
	if (is_tag_type(type))
		collect_dependencies(parent, type, decl_enough);
	else
		collect_dependencies(parent, show_base_type(type), decl_enough);
	if (!typedef_symbol)
		return;
	dependency = add_dependency(parent, typedef_symbol, decl_enough);
	if (dependency)
		collect_symbol_dependencies(dependency);
}

struct output_dependencies {
	struct symbol_list *definitions;
	struct symbol_list *declarations;
};

static struct output_dependencies printed;

/*
 * Named symbols are the same if they have the same name and namespace,
 * so a redeclaration is not printed twice.  Function-local tags and
 * anonymous symbols are looked up by pointer.
 */
struct printed_symbol {
	void *key;
	enum namespace namespace;
	bool defined;
	bool declared;
};
DECLARE_PTR_LIST(printed_symbol_list, struct printed_symbol);

#define PRINTED_HASH_SIZE 4096
static struct printed_symbol_list *printed_hash[PRINTED_HASH_SIZE];

struct tag_output {
	struct code_buffer *out;
	struct symbol_list *dependency_types;
};

static void show_struct_definition(struct tag_output *output, struct symbol *type);
static bool show_tag_definition(struct tag_output *output, struct symbol *type);
static void show_type_definition(struct tag_output *output, struct symbol *symbol);
static void show_type_declaration(struct tag_output *output, struct symbol *symbol);
static void show_typedef_definition(struct code_buffer *out, struct symbol *type);
static void show_type_dependencies(struct tag_output *output, struct symbol *type, bool decl_enough);
static void show_type_members(struct tag_output *output, struct symbol *type);

/* Function-local tags with the same name are different types. */
static bool is_local_tag(struct symbol *type)
{
	return is_tag_type(type) && type->scope && !toplevel(type->scope);
}

static struct printed_symbol *printed_symbol(struct symbol *symbol)
{
	struct printed_symbol_list **bucket;
	struct printed_symbol *entry;
	enum namespace namespace = NS_NONE;
	void *key = symbol;

	if (symbol->ident && !is_local_tag(symbol)) {
		key = symbol->ident;
		namespace = symbol->namespace;
	}
	bucket = &printed_hash[((unsigned long)key / sizeof(void *) +
				namespace) % PRINTED_HASH_SIZE];
	FOR_EACH_PTR(*bucket, entry) {
		if (entry->key == key && entry->namespace == namespace)
			return entry;
	} END_FOR_EACH_PTR(entry);
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		die("out of memory");
	entry->key = key;
	entry->namespace = namespace;
	add_ptr_list(bucket, entry);
	return entry;
}

static void free_printed(void)
{
	struct printed_symbol *entry;
	int i;

	free_ptr_list(&printed.definitions);
	free_ptr_list(&printed.declarations);
	for (i = 0; i < PRINTED_HASH_SIZE; i++) {
		FOR_EACH_PTR(printed_hash[i], entry) {
			free(entry);
		} END_FOR_EACH_PTR(entry);
		free_ptr_list(&printed_hash[i]);
	}
}

static void remember_definition(struct symbol *symbol)
{
	struct printed_symbol *entry = printed_symbol(symbol);

	if (entry->defined)
		return;
	entry->defined = true;
	add_symbol(&printed.definitions, symbol);
}

static void remember_declaration(struct symbol *symbol)
{
	struct printed_symbol *entry = printed_symbol(symbol);

	if (entry->declared)
		return;
	entry->declared = true;
	add_symbol(&printed.declarations, symbol);
}

static bool symbol_was_defined(struct symbol *symbol)
{
	return printed_symbol(symbol)->defined;
}

static bool symbol_was_declared(struct symbol *symbol)
{
	struct printed_symbol *entry = printed_symbol(symbol);

	return entry->defined || entry->declared;
}

static bool tag_was_defined(struct symbol *type)
{
	return symbol_was_defined(type);
}

static bool tag_was_declared(struct symbol *type)
{
	return symbol_was_declared(type);
}

static void show_type_declaration(struct tag_output *output,
				  struct symbol *symbol)
{
	struct symbol *tag;

	if (is_type_definition(symbol, &tag))
		symbol = tag;
	if (!is_tag_type(symbol) || !symbol->ident ||
	    tag_was_declared(symbol))
		return;
	append(output->out, "%s %s;\n", tag_type_name(symbol), show_ident(symbol->ident));
	remember_declaration(symbol);
}

static void show_required_type(struct tag_output *output,
			       struct symbol *needed, bool decl_enough)
{
	if (needed->type == SYM_ENUM && !tag_has_definition(needed)) {
		show_type_declaration(output, needed);
		return;
	}
	/* Named tags are spelled "struct x", so a declaration is enough. */
	if ((is_typedef_tag(needed) && needed->namespace != NS_STRUCT) ||
	    !decl_enough) {
		if (!tag_was_defined(needed)) {
			if (tag_has_definition(needed))
				show_type_definition(output, needed);
			else
				fprintf(stderr, "%s: missing definition for %s %s\n",
					base_filename, tag_type_name(needed),
					show_ident(needed->ident));
		}
		return;
	}
	show_type_declaration(output, needed);
}

static bool dependency_is_type(struct symbol *symbol)
{
	return symbol->namespace == NS_TYPEDEF || is_tag_type(symbol) ||
	       has_type_expression(symbol);
}

static void show_function_forwards(struct code_buffer *out, struct symbol *type)
{
	struct sym_dependency root = { .sym = type, .root = true };

	collect_function_type_dependencies(&root, type);
	show_dependencies(out, &root);
}

/*
 * A function or variable declaration is enough unless the type cannot be
 * spelled without the definition, or sizeof() needs an array bound which
 * only the initializer gives.
 */
static bool symbol_declaration_is_enough(struct symbol *symbol, bool need_size)
{
	struct symbol *type = strip_type_wrappers(symbol);

	/* A typeof() declaration is handled as a type. */
	if (has_type_expression(symbol))
		return type_can_use_forward_declaration(symbol);
	if (type->type == SYM_FN)
		return true;
	if (find_anonymous_tag(symbol->ctype.base_type))
		return false;
	return !need_size || !is_incomplete_array_declaration(symbol);
}

static void add_symbol_dependency(struct sym_dependency *parent,
				  struct symbol *symbol, bool need_size)
{
	struct sym_dependency *dependency;
	struct symbol *type;

	if (!symbol || !symbol->ident ||
	    strncmp(symbol->ident->name, "__builtin_", 10) == 0)
		return;
	type = strip_type_wrappers(symbol);
	/*
	 * A function-typed parameter or typeof() local is not a function
	 * declaration.  Local function declarations are marked extern.
	 */
	if (!type ||
	    ((type->type != SYM_FN ||
	      (symbol->scope && !toplevel(symbol->scope))) &&
	     symbol->scope != file_scope &&
	     !(symbol->ctype.modifiers & (MOD_TOPLEVEL | MOD_EXTERN))))
		return;

	dependency = add_dependency(parent, symbol,
				    symbol_declaration_is_enough(symbol, need_size));
	if (dependency)
		collect_symbol_dependencies(dependency);
}

static void collect_complete_type_dependency(struct sym_dependency *parent,
					     struct symbol *type)
{
	type = strip_type_wrappers(type);
	while (type_is_ptr(type) || type_is_array(type))
		type = strip_type_wrappers(show_base_type(type));
	add_type_dependency(parent, type, false);
	collect_dependencies(parent, type, false);
}

static void collect_pointer_element_dependency(struct sym_dependency *parent,
							struct symbol *type)
{
	type = strip_type_wrappers(type);
	if (type_is_array(type) || type_is_ptr(type))
		type = strip_type_wrappers(show_base_type(type));
	else
		return;

	add_type_dependency(parent, type,
			    type_can_use_forward_declaration(type));
	collect_dependencies(parent, type,
			     type_can_use_forward_declaration(type));
}

static void collect_function_return_type_dependency(struct sym_dependency *parent,
						     struct symbol *function)
{
	struct symbol *type;

	if (!function)
		return;

	type = strip_type_wrappers(function->ctype.base_type);
	while (type_is_ptr(type) || type_is_array(type))
		type = strip_type_wrappers(show_base_type(type));
	if (type && type->type == SYM_FN)
		collect_complete_type_dependency(parent, type->ctype.base_type);
}

static struct symbol *find_member_symbol_rec(struct symbol *type,
					     struct ident *ident,
					     struct symbol_list **seen)
{
	struct symbol *member;
	struct symbol *nested;

	type = tag_type(type);
	if (!type || !ident ||
	    lookup_ptr_list_entry((struct ptr_list *)*seen, type))
		return NULL;
	add_symbol(seen, type);
	FOR_EACH_PTR(type->symbol_list, member) {
		if (member->ident == ident)
			return member;
		nested = tag_type(member);
		if (nested && !nested->ident && !member->ident) {
			member = find_member_symbol_rec(nested, ident, seen);
			if (member)
				return member;
		}
	} END_FOR_EACH_PTR(member);
	return NULL;
}

static struct symbol *find_member_symbol(struct symbol *type,
					 struct ident *ident)
{
	struct symbol_list *seen = NULL;

	return find_member_symbol_rec(type, ident, &seen);
}

static struct symbol *collect_expression_dependencies_rec(struct sym_dependency *dependency,
							  struct expression *e,
							  bool complete_type)
{
	struct expression *item;
	struct type_expression *map;
	struct symbol *type;
	struct symbol *member;
	struct symbol *expression_type;

	if (!e)
		return NULL;
	type = e->ctype;
	if (complete_type)
		collect_complete_type_dependency(dependency, e->ctype);
	switch (e->type) {
	case EXPR_TYPE:
		if (!type)
			type = e->symbol;
		add_type_dependency(dependency, e->symbol, false);
		if (complete_type)
			collect_complete_type_dependency(dependency, e->symbol);
		collect_type_expression_dependencies(dependency, e->symbol);
		break;
	case EXPR_PREOP:
	case EXPR_POSTOP:
		item = e->unop;
		expression_type = collect_expression_dependencies_rec(dependency, item,
								      complete_type);
		if (e->op == SPECIAL_INCREMENT || e->op == SPECIAL_DECREMENT)
			collect_pointer_element_dependency(dependency, expression_type);
		if (!type) {
			if (e->op == '*' && expression_type) {
				type = strip_type_wrappers(expression_type);
				type = strip_type_wrappers(show_base_type(type));
			} else if (e->op == '&' && expression_type) {
				type = alloc_symbol(e->pos, SYM_PTR);
				type->ctype.base_type = expression_type;
			} else {
				type = expression_type;
			}
		}
		break;
	case EXPR_BINOP:
	case EXPR_ASSIGNMENT:
	case EXPR_LOGICAL:
	case EXPR_COMMA:
	case EXPR_COMPARE:
		type = collect_expression_dependencies_rec(dependency, e->left,
							   complete_type);
		expression_type = collect_expression_dependencies_rec(dependency,
								      e->right,
								      complete_type);
		if (e->type == EXPR_BINOP && (e->op == '+' || e->op == '-')) {
			collect_pointer_element_dependency(dependency, type);
			collect_pointer_element_dependency(dependency, expression_type);
		}
		if (e->type == EXPR_ASSIGNMENT &&
		    (e->op == SPECIAL_ADD_ASSIGN || e->op == SPECIAL_SUB_ASSIGN))
			collect_pointer_element_dependency(dependency, type);
		/* Copying a struct or union needs its layout. */
		if (e->type == EXPR_ASSIGNMENT &&
		    is_struct_or_union(strip_type_wrappers(type)))
			collect_complete_type_dependency(dependency, type);
		if (e->ctype)
			type = e->ctype;
		break;
	case EXPR_DEREF:
		expression_type = collect_expression_dependencies_rec(dependency,
								      e->deref,
								      complete_type || e->member);
		if (e->member) {
			struct symbol *tag = tag_type(expression_type);

			member = find_member_symbol(tag, e->member);
			if (tag)
				collect_complete_type_dependency(dependency, tag);
			if (member && !type) {
				type = member->ctype.base_type;
				if (complete_type)
					collect_complete_type_dependency(dependency,
									 type);
			}
		} else if (!type && expression_type) {
			type = strip_type_wrappers(show_base_type(expression_type));
		}
		break;
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
	case EXPR_PTRSIZEOF:
		if (!type)
			type = e->cast_type;
		add_type_dependency(dependency, e->cast_type, false);
		collect_type_expression_dependencies(dependency, e->cast_type);
		if (complete_type)
			collect_complete_type_dependency(dependency, e->cast_type);
		collect_expression_dependencies_rec(dependency, e->cast_expression,
						    complete_type);
		break;
	case EXPR_SIZEOF:
	case EXPR_ALIGNOF:
		if (!type)
			type = e->cast_type;
		add_type_dependency(dependency, e->cast_type, false);
		collect_type_expression_dependencies(dependency, e->cast_type);
		if (e->cast_type)
			collect_complete_type_dependency(dependency, e->cast_type);
		expression_type = collect_expression_dependencies_rec(dependency,
							       e->cast_expression,
							       complete_type);
		item = e->cast_expression;
		while (item && item->type == EXPR_PREOP && item->op == '(')
			item = item->unop;
		if (item && item->type == EXPR_SYMBOL)
			add_symbol_dependency(dependency, item->symbol, true);
		if (expression_type &&
		    !type_is_ptr(strip_type_wrappers(expression_type)))
			collect_complete_type_dependency(dependency, expression_type);
		break;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		collect_expression_dependencies_rec(dependency, e->conditional,
						    complete_type);
		collect_expression_dependencies_rec(dependency, e->cond_true,
						    complete_type);
		collect_expression_dependencies_rec(dependency, e->cond_false,
						    complete_type);
		break;
	case EXPR_STATEMENT:
		collect_statement_dependencies(dependency, e->statement);
		expression_type = collect_statement_result_dependencies(dependency,
									e->statement,
									complete_type);
		if (!type)
			type = expression_type;
		break;
	case EXPR_CALL:
		item = e->fn;
		while (item && item->type == EXPR_PREOP &&
		       (item->op == '*' || item->op == '('))
			item = item->unop;
		if (item && item->type == EXPR_SYMBOL && item->symbol) {
			add_symbol_dependency(dependency, item->symbol, false);
			collect_function_type_dependencies(dependency,
						   item->symbol->ctype.base_type);
			if (complete_type)
				collect_function_return_type_dependency(dependency,
								item->symbol);
			expression_type = strip_type_wrappers(item->symbol->ctype.base_type);
			while (type_is_ptr(expression_type))
				expression_type =
					strip_type_wrappers(show_base_type(expression_type));
			if (!type && expression_type && expression_type->type == SYM_FN)
				type = expression_type->ctype.base_type;
		}
		expression_type = collect_expression_dependencies_rec(dependency,
								      e->fn,
								      complete_type);
		/* A call through a function pointer has no symbol to look at. */
		if (!type && expression_type) {
			expression_type = strip_type_wrappers(expression_type);
			while (type_is_ptr(expression_type))
				expression_type =
					strip_type_wrappers(show_base_type(expression_type));
			if (expression_type && expression_type->type == SYM_FN) {
				type = expression_type->ctype.base_type;
				if (complete_type)
					collect_complete_type_dependency(dependency,
									 type);
			}
		}
		/* Using the result does not require complete argument types. */
		FOR_EACH_PTR(e->args, item) {
			collect_expression_dependencies_rec(dependency, item,
							    false);
		} END_FOR_EACH_PTR(item);
		break;
	case EXPR_SYMBOL:
		if (e->symbol) {
			if (!type && e->symbol->ctype.base_type == &autotype_ctype)
				type = collect_expression_dependencies_rec(dependency,
						symbol_initializer(e->symbol), false);
			if (!type)
				type = e->symbol->ctype.base_type;
			if (complete_type)
				collect_complete_type_dependency(dependency,
							 e->symbol->ctype.base_type);
			else
				add_type_dependency(dependency, e->symbol,
						type_can_use_forward_declaration(e->symbol));
		}
		add_symbol_dependency(dependency, e->symbol, false);
		break;
	case EXPR_INITIALIZER:
		FOR_EACH_PTR(e->expr_list, item) {
			collect_expression_dependencies_rec(dependency, item,
							    complete_type);
		} END_FOR_EACH_PTR(item);
		break;
	case EXPR_IDENTIFIER:
		collect_expression_dependencies_rec(dependency, e->ident_expression,
						    complete_type);
		break;
	case EXPR_INDEX:
		collect_expression_dependencies_rec(dependency, e->idx_expression,
						    complete_type);
		break;
	case EXPR_POS:
		collect_expression_dependencies_rec(dependency, e->init_expr,
						    complete_type);
		break;
	case EXPR_SLICE:
		collect_expression_dependencies_rec(dependency, e->base, complete_type);
		break;
	case EXPR_OFFSETOF:
		collect_complete_type_dependency(dependency, e->in);
		collect_type_expression_dependencies(dependency, e->in);
		for (item = e; item; item = item->down) {
			if (item->op != '.')
				collect_expression_dependencies_rec(dependency, item->index,
								    complete_type);
		}
		break;
	case EXPR_GENERIC:
		collect_expression_dependencies_rec(dependency, e->control,
						    complete_type);
		for (map = e->map; map; map = map->next) {
			add_type_dependency(dependency, map->type, false);
			collect_type_expression_dependencies(dependency, map->type);
			expression_type = collect_expression_dependencies_rec(dependency,
									      map->expr,
									      complete_type);
			if (!type)
				type = expression_type;
		}
		expression_type = collect_expression_dependencies_rec(dependency,
								      e->def,
								      complete_type);
		if (!type)
			type = expression_type;
		break;
	default:
		break;
	}
	return type;
}

static void collect_expression_dependencies(struct sym_dependency *dependency,
					    struct expression *e)
{
	collect_expression_dependencies_rec(dependency, e, false);
}

static struct symbol *collect_statement_result_dependencies(struct sym_dependency *dependency,
							   struct statement *s,
							   bool complete_type)
{
	struct statement *last;

	if (!s)
		return NULL;
	switch (s->type) {
	case STMT_COMPOUND:
		last = last_ptr_list((struct ptr_list *)s->stmts);
		return collect_statement_result_dependencies(dependency, last,
							     complete_type);
	case STMT_EXPRESSION:
	case STMT_CONTEXT:
		return collect_expression_dependencies_rec(dependency,
							   s->expression,
							   complete_type);
	case STMT_LABEL:
		return collect_statement_result_dependencies(dependency,
							     s->label_statement,
							     complete_type);
	default:
		return NULL;
	}
}

static void collect_type_expression_dependencies(struct sym_dependency *dependency,
						 struct symbol *type)
{
	struct sym_dependency *type_dependency;
	bool decl_enough = false;

	while (type) {
		if (has_type_expression(type)) {
			type_dependency = add_dependency(dependency, type,
							 decl_enough);
			if (type_dependency)
				collect_symbol_dependencies(type_dependency);
		}
		if (type_is_ptr(type))
			decl_enough = true;
		type = show_base_type(type);
	}
}

static void collect_declaration_dependencies(struct sym_dependency *dependency,
					     struct symbol_list *declarations)
{
	struct symbol *declaration;

	FOR_EACH_PTR(declarations, declaration) {
		add_type_dependency(dependency, declaration->ctype.base_type,
				    type_can_use_forward_declaration(declaration));
		collect_type_expression_dependencies(dependency, declaration);
		collect_expression_dependencies(dependency,
						symbol_initializer(declaration));
	} END_FOR_EACH_PTR(declaration);
}

static void show_dependencies(struct code_buffer *out,
			      struct sym_dependency *parent);

static void show_dependency_cycle(struct code_buffer *out,
				  struct sym_dependency *dependency)
{
	struct tag_output output = { .out = out };
	struct symbol *symbol = dependency->sym;

	/* Anonymous tags have no declaration form; leave their definition to sym(). */
	if (find_anonymous_tag(symbol->ctype.base_type))
		return;
	if (has_type_expression(symbol))
		return;
	if (is_tag_type(symbol)) {
		if (!dependency->decl_enough) {
			fprintf(stderr, "cannot forward declare dependency %s\n",
				symbol->ident ? show_ident(symbol->ident) : "<noident>");
			return;
		}
		show_type_declaration(&output, symbol);
		return;
	}
	if (symbol->namespace == NS_TYPEDEF) {
		show_type_definition(&output, symbol);
		return;
	}
	show_symbol_declaration(out, symbol);
}

struct symbol *show_code_minimal_root;

/*
 * With --minimal, everything which can be declared is declared, except the
 * root function and inline functions.
 */
static bool minimal_declaration(struct sym_dependency *entry)
{
	struct symbol *symbol = entry->sym;
	struct symbol *definition;

	if (!show_code_minimal_root || !entry->decl_enough ||
	    dependency_is_type(symbol))
		return false;
	if (strip_type_wrappers(symbol)->type != SYM_FN)
		return true;
	definition = function_definition(symbol) ?: symbol;
	if (declaration_has_typeof(definition))
		return false;
	if (definition == function_definition(show_code_minimal_root))
		return false;
	return !((symbol->ctype.modifiers | definition->ctype.modifiers) &
		 MOD_INLINE);
}

static void show_minimal_declaration(struct code_buffer *out,
				     struct symbol *symbol)
{
	struct symbol *type = strip_type_wrappers(symbol);
	struct symbol copy;

	/* "extern typeof(f) f;" needs the declaration of f. */
	if (type->type == SYM_FN)
		symbol = function_definition(symbol) ?: symbol;
	if (symbol_was_declared(symbol) || symbol_was_defined(symbol))
		return;
	if (type->type == SYM_FN)
		show_function_forwards(out, symbol->ctype.base_type);
	copy = *symbol;
	/* GCC warns about a static function which is used but not defined. */
	copy.ctype.modifiers &= ~MOD_STATIC;
	copy.ctype.modifiers |= MOD_EXTERN;
	show_c_typename(out, &copy);
	show_asm_name(out, &copy);
	append(out, ";\n");
	remember_declaration(symbol);
}

static void show_dependency_entry(struct code_buffer *out,
				  struct sym_dependency *entry)
{
	struct tag_output type_output = { .out = out };
	struct symbol *dependency;
	struct symbol *definition;
	struct symbol *type;
	struct symbol copy;
	bool declare;

	show_dependencies(out, entry);
	dependency = entry->sym;
	if (has_type_expression(dependency))
		return;
	if (dependency_is_type(dependency)) {
		show_type_dependencies(&type_output, dependency, entry->decl_enough);
		return;
	}
	if (minimal_declaration(entry)) {
		show_minimal_declaration(out, dependency);
		return;
	}
	/*
	 * A declaration would be enough, but define what we can.  Only a
	 * pointer or an extern struct without a definition is declared.
	 */
	declare = type_can_use_forward_declaration(dependency);
	if (declare && symbol_was_declared(dependency))
		return;
	if (!declare) {
		sym(out, dependency);
		return;
	}
	type = strip_type_wrappers(dependency);
	access_symbol(dependency);
	definition = function_definition(dependency);
	if (definition) {
		sym(out, definition);
		return;
	}
	if (type->type != SYM_FN &&
	    (dependency->ctype.modifiers & MOD_STATIC)) {
		sym(out, dependency);
		return;
	}
	if (symbol_initializer(dependency)) {
		sym(out, dependency);
		return;
	}
	if (type->type == SYM_FN)
		show_function_forwards(out, dependency->ctype.base_type);
	copy = *dependency;
	if (!(copy.ctype.modifiers & MOD_STATIC))
		copy.ctype.modifiers |= MOD_EXTERN;
	show_c_typename(out, &copy);
	show_asm_name(out, &copy);
	append(out, ";\n");
	remember_declaration(dependency);
}

static void show_dependency(struct code_buffer *out,
			    struct sym_dependency *entry)
{
	if (!entry || !entry->sym)
		return;
	if (dependency_has_ancestor(entry)) {
		show_dependency_cycle(out, entry);
		return;
	}
	/* A shared node only needs to be printed once. */
	if (entry->shown)
		return;
	show_dependency_entry(out, entry);
	entry->shown = true;
}

static void show_dependencies(struct code_buffer *out,
			      struct sym_dependency *parent)
{
	struct sym_dependency *dependency;
	struct symbol_list *saved = NULL;

	/* Each root starts a new chain of ancestors. */
	if (parent->root) {
		saved = showing;
		showing = NULL;
	}
	add_symbol(&showing, parent->sym);
	FOR_EACH_PTR(parent->depends, dependency) {
		show_dependency(out, dependency);
	} END_FOR_EACH_PTR(dependency);
	delete_ptr_list_last((struct ptr_list **)&showing);
	if (parent->root) {
		free_ptr_list(&showing);
		showing = saved;
	}
}

static void show_initializer_dependencies_rec(struct code_buffer *out,
					      struct symbol *symbol,
					      struct symbol_list **active)
{
	struct sym_dependency root = { .sym = symbol, .root = true };

	(void)active;
	collect_symbol_dependencies(&root);
	show_dependencies(out, &root);
}

static void show_initializer_dependencies(struct code_buffer *out,
					  struct symbol *symbol)
{
	struct symbol_list *active = NULL;

	show_initializer_dependencies_rec(out, symbol, &active);
}

static void collect_asm_dependencies(struct sym_dependency *dependency,
				     struct asm_operand_list *operands)
{
	struct asm_operand *operand;

	FOR_EACH_PTR(operands, operand) {
		collect_expression_dependencies(dependency, operand->expr);
	} END_FOR_EACH_PTR(operand);
}

static void collect_statement_dependencies(struct sym_dependency *dependency,
					   struct statement *s)
{
	struct statement *child;
	struct expression *clobber;

	if (!s)
		return;

	switch (s->type) {
	case STMT_COMPOUND:
		FOR_EACH_PTR(s->stmts, child) {
			collect_statement_dependencies(dependency, child);
		} END_FOR_EACH_PTR(child);
		break;
	case STMT_DECLARATION:
		if (s->tag)
			collect_dependencies(dependency, s->tag, false);
		collect_declaration_dependencies(dependency, s->declaration);
		break;
	case STMT_EXPRESSION:
	case STMT_CONTEXT:
		collect_expression_dependencies(dependency, s->expression);
		break;
	case STMT_RETURN:
		collect_expression_dependencies(dependency, s->ret_value);
		break;
	case STMT_IF:
		collect_expression_dependencies(dependency, s->if_conditional);
		collect_statement_dependencies(dependency, s->if_true);
		collect_statement_dependencies(dependency, s->if_false);
		break;
	case STMT_SWITCH:
		collect_expression_dependencies(dependency, s->switch_expression);
		collect_statement_dependencies(dependency, s->switch_statement);
		break;
	case STMT_CASE:
		collect_expression_dependencies(dependency, s->case_expression);
		collect_expression_dependencies(dependency, s->case_to);
		collect_statement_dependencies(dependency, s->case_statement);
		break;
	case STMT_ITERATOR:
		collect_declaration_dependencies(dependency, s->iterator_syms);
		collect_statement_dependencies(dependency, s->iterator_pre_statement);
		collect_expression_dependencies(dependency, s->iterator_pre_condition);
		collect_statement_dependencies(dependency, s->iterator_statement);
		collect_statement_dependencies(dependency, s->iterator_post_statement);
		collect_expression_dependencies(dependency, s->iterator_post_condition);
		break;
	case STMT_LABEL:
		collect_statement_dependencies(dependency, s->label_statement);
		break;
	case STMT_GOTO:
		collect_expression_dependencies(dependency, s->goto_expression);
		break;
	case STMT_ASM:
		collect_expression_dependencies(dependency, s->asm_string);
		collect_asm_dependencies(dependency, s->asm_outputs);
		collect_asm_dependencies(dependency, s->asm_inputs);
		FOR_EACH_PTR(s->asm_clobbers, clobber) {
			collect_expression_dependencies(dependency, clobber);
		} END_FOR_EACH_PTR(clobber);
		break;
	case STMT_RANGE:
		collect_expression_dependencies(dependency, s->range_expression);
		collect_expression_dependencies(dependency, s->range_low);
		collect_expression_dependencies(dependency, s->range_high);
		break;
	default:
		break;
	}
}

static void collect_symbol_dependencies(struct sym_dependency *dependency)
{
	struct symbol *symbol;
	struct symbol *type;
	struct symbol *member;
	struct statement *stmt;

	if (!dependency || dependency->collected)
		return;
	dependency->collected = true;
	symbol = dependency->sym;
	if (!symbol)
		return;
	if (has_type_expression(symbol)) {
		collect_expression_dependencies_rec(dependency,
					    symbol->type_expression, false);
		type = show_base_type(symbol);
		if (type != symbol)
			collect_dependencies(dependency, type,
					     dependency->decl_enough);
		return;
	}
	if (is_tag_type(symbol)) {
		if (dependency->decl_enough)
			return;
		FOR_EACH_PTR(symbol->symbol_list, member) {
			add_type_dependency(dependency, member,
					    type_can_use_forward_declaration(member));
			collect_expression_dependencies(dependency,
						symbol_initializer(member));
		} END_FOR_EACH_PTR(member);
		return;
	}
	if (symbol->namespace == NS_TYPEDEF) {
		collect_dependencies(dependency,
				     show_base_type(symbol->ctype.base_type), false);
		collect_type_expression_dependencies(dependency, symbol);
		return;
	}
	type = symbol->ctype.base_type;
	if (!type)
		return;
	if (type->type == SYM_FN) {
		collect_function_type_dependencies(dependency, type);
		collect_type_expression_dependencies(dependency, symbol);
		if (!dependency->root && minimal_declaration(dependency))
			return;
		stmt = type->stmt ?: type->inline_stmt;
		collect_statement_dependencies(dependency, stmt);
		return;
	}
	add_type_dependency(dependency, type,
			    type_can_use_forward_declaration(symbol));
	collect_type_expression_dependencies(dependency, symbol);
	if (!dependency->root && minimal_declaration(dependency))
		return;
	collect_expression_dependencies(dependency, symbol_initializer(symbol));
}

static void show_function_dependencies(struct code_buffer *out,
				       struct symbol *function)
{
	struct sym_dependency root = { .sym = function, .root = true };

	collect_symbol_dependencies(&root);
	show_dependencies(out, &root);
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
		struct sym_dependency root = { .sym = member, .root = true };

		collect_type_expression_dependencies(&root, member);
		show_dependencies(output->out, &root);
		show_type_dependencies(output, member,
				       type_can_use_forward_declaration(member));
	} END_FOR_EACH_PTR(member);
}

static void show_type_dependencies(struct tag_output *output,
				   struct symbol *type, bool decl_enough)
{
	struct symbol *arg;
	struct symbol *function_type;
	struct symbol *nested_type;
	struct symbol *needed;
	struct symbol *tag;
	struct symbol *typedef_symbol;

	if (!type)
		return;

	if (type->namespace == NS_TYPEDEF) {
		show_type_definition(output, type);
		return;
	}
	typedef_symbol = type_typedef_symbol(type);
	nested_type = strip_bitfield_type(type);
	if (!nested_type)
		return;
	if (!typedef_symbol && nested_type->ident) {
		typedef_symbol = lookup_symbol(nested_type->ident, NS_TYPEDEF);
		/* "typedef string path;" is not a name for "struct path". */
		if (typedef_symbol &&
		    tag_type(typedef_symbol->ctype.base_type) != tag_type(nested_type))
			typedef_symbol = NULL;
	}
	if (typedef_symbol && typedef_symbol != type) {
		if (is_type_definition(typedef_symbol, &tag)) {
			if (!tag_was_defined(tag))
				show_type_definition(output, tag);
			return;
		}
		if (tag_was_defined(typedef_symbol))
			return;
		remember_definition(typedef_symbol);
		show_type_dependencies(output, typedef_symbol->ctype.base_type,
				       true);
		show_typedef_definition(output->out, typedef_symbol);
		return;
	}

	needed = tag_dependency_symbol(nested_type);
	if (needed) {
		show_required_type(output, needed, decl_enough);
		return;
	}
	if (is_struct_or_union(nested_type) && !nested_type->ident) {
		show_type_members(output, nested_type);
		return;
	}

	function_type = nested_type;
	while (type_is_ptr(function_type) || type_is_array(function_type))
		function_type = strip_type_wrappers(show_base_type(function_type));

	if (function_type && function_type->type == SYM_FN) {
		show_type_dependencies(output, function_type->ctype.base_type,
				       type_can_use_forward_declaration(
					       function_type->ctype.base_type));
		FOR_EACH_PTR(function_type->arguments, arg) {
			show_type_dependencies(output, arg,
					       type_can_use_forward_declaration(arg));
		} END_FOR_EACH_PTR(arg);
		return;
	}

	if (type_is_ptr(nested_type) || type_is_array(nested_type))
		show_type_dependencies(output, show_base_type(nested_type),
				       type_can_use_forward_declaration(nested_type));
}

/* C rendering. */

static void type_name_init(struct c_type_name *name)
{
	name->size = 1024;
	name->buf = malloc(name->size);
	if (!name->buf)
		die("out of memory");
	name->start = name->end = name->buf + name->size / 2;
}

static void type_name_free(struct c_type_name *name)
{
	free(name->buf);
}

/* Leave room for "before" bytes, "after" bytes and a NUL terminator. */
static void type_name_reserve(struct c_type_name *name, size_t before,
			      size_t after)
{
	size_t used = name->end - name->start;
	size_t size;
	char *buf;

	if (name->start - name->buf >= before &&
	    name->buf + name->size - name->end > after)
		return;
	size = 2 * (used + before + after + 1);
	buf = malloc(size);
	if (!buf)
		die("out of memory");
	memcpy(buf + before + (size - used - before - after - 1) / 2,
	       name->start, used);
	free(name->buf);
	name->buf = buf;
	name->size = size;
	name->start = buf + before + (size - used - before - after - 1) / 2;
	name->end = name->start + used;
}

static void type_name_prepend(struct c_type_name *name, const char *text)
{
	size_t len = strlen(text);

	type_name_reserve(name, len, 0);
	name->start -= len;
	memcpy(name->start, text, len);
}

static void type_name_append(struct c_type_name *name, const char *text)
{
	size_t len = strlen(text);

	type_name_reserve(name, 0, len);
	memcpy(name->end, text, len);
	name->end += len;
}

static void type_name_prepend_format(struct c_type_name *name,
				     const char *format, ...)
{
	va_list args;
	char *buffer;

	va_start(args, format);
	if (vasprintf(&buffer, format, args) < 0)
		die("out of memory");
	va_end(args);
	type_name_prepend(name, buffer);
	free(buffer);
}

static void type_name_append_format(struct c_type_name *name,
				    const char *format, ...)
{
	va_list args;
	char *buffer;

	va_start(args, format);
	if (vasprintf(&buffer, format, args) < 0)
		die("out of memory");
	va_end(args);
	type_name_append(name, buffer);
	free(buffer);
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
		{ MOD_USED, "__attribute__((__used__))" },
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
	/* The typedef is not declared yet inside "typedef struct x {...} x;" */
	if (type->namespace != NS_STRUCT && is_typedef_tag(type))
		type_name_prepend_format(name, "%s", show_ident(type->ident));
	else
		type_name_prepend_format(name, "%s %s", tag_type_name(type),
					 show_ident(type->ident));
	type_name_prepend(name, show_gcc_modifiers(modifiers &
		(MOD_QUALIFIER | MOD_ATOMIC)));
}

static bool has_type_expression(struct symbol *type)
{
	return type && type->type_expression &&
	       type->type_expression->type != EXPR_INITIALIZER;
}

static bool is_va_list_use(struct symbol *symbol)
{
	/* Sparse lowers __builtin_va_list to the shared ptr_ctype. */
	for (; symbol; symbol = show_base_type(symbol)) {
		if (symbol == &ptr_ctype)
			return true;
		if (symbol->type != SYM_NODE && symbol->type != SYM_PTR)
			return false;
	}
	return false;
}

static struct symbol *show_base_type(struct symbol *type)
{
	if (type && type->orig_base_type)
		return type->orig_base_type;
	return type ? type->ctype.base_type : NULL;
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

static bool declaration_has_typeof(struct symbol *type)
{
	for (; type; type = show_base_type(type)) {
		if (is_typeof(type))
			return true;
		if (type->type == SYM_FN)
			return false;
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
			struct code_buffer expression_out = {
				.len = C_TYPE_NAME_BUFFER_SIZE,
				.line_start = true,
				.grow = true,
			};
			const char *modifier_string;

			expression_out.buf = malloc(expression_out.len);
			if (!expression_out.buf)
				die("out of memory");
			expression_out.buf[0] = '\0';
			show_typeof_expression(&expression_out, type);
			/* typeof() already carries the integer specifier. */
			modifiers &= ~MOD_SPECIFIER;
			if (name->start != name->end)
				type_name_prepend(name, " ");
			type_name_prepend_format(name, "%s(%s)",
				type->typeof_is_unqual ||
				type->type == SYM_TYPEOF_UNQUAL ?
				"__typeof_unqual__" : "typeof",
				expression_out.buf);
			free(expression_out.buf);
			modifier_string = show_gcc_modifiers(modifiers);
			type_name_prepend(name, modifier_string);
			break;
		}
		typename = builtin_typename(type);
		if (typename) {
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

			if (type == &ptr_ctype) {
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
				struct c_type_name arg_name;

				type_name_init(&arg_name);
				show_direct_c_typename(out, arg, &arg_name);
				*arg_name.end = '\0';
				type_name_append_format(name, "%s%s", comma ? ", " : "",
							arg_name.start);
				type_name_free(&arg_name);
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
			/* Sparse's __bitwise means nothing to GCC. */
			if (!type->ident)
				break;
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
	if (fouled)
		type_name_prepend(name, "fouled ");
	if (name->end > name->start && name->end[-1] == ' ')
		name->end--;
}

static void show_c_typename(struct code_buffer *out, struct symbol *s)
{
	struct c_type_name name;

	type_name_init(&name);
	show_direct_c_typename(out, s, &name);
	*name.end = '\0';
	append(out, "%s", name.start);
	type_name_free(&name);
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

static bool has_negate_parent(struct expression *e)
{
	struct expression *parent = e;

	do {
		if (!(parent->parent & 0x1UL))
			return false;
		parent = (struct expression *)(parent->parent & ~0x1UL);
	} while (parent->type == EXPR_IMPLIED_CAST);

	return parent->type == EXPR_PREOP && parent->op == '-';
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

	if ((e->flags & CEF_CHAR) && value >= ' ' && value <= '~') {
		if (value == '\\' || value == '\'')
			append(out, "'\\%c'", (int)value);
		else
			append(out, "'%c'", (int)value);
		return;
	}
	if (!e->ctype || e->ctype != &int_ctype)
		goto hex;
	if (has_bitwise_parent(e))
		goto hex;
	if (is_signed_type(e->ctype) &&
	    signed_value >= -4059 && signed_value < 0) {
		if (has_negate_parent(e))
			append(out, "(%lld%s)", signed_value, suffix);
		else
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
		if (part->op == '.') {
			append(out, "%s%s", first ? "" : dereference ? "->" : ".",
			       show_ident(part->ident));
		} else {
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

static bool show_indexed_dereference(struct code_buffer *out,
				     struct expression *e)
{
	while (e && e->type == EXPR_PREOP && e->op == '(')
		e = e->unop;
	if (!e || e->type != EXPR_BINOP || e->op != '+')
		return false;
	/* "&p->member" is printed without parentheses. */
	if (e->left && e->left->type == EXPR_PREOP && e->left->op == '&') {
		append(out, "(");
		expr(out, e->left);
		append(out, ")");
	} else {
		expr(out, e->left);
	}
	append(out, "[");
	expr(out, e->right);
	append(out, "]");
	return true;
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
	if (e && (e->type == EXPR_IDENTIFIER || e->type == EXPR_INDEX)) {
		show_initializer_designator(out, e);
	} else {
		append(out, " = ");
		expr(out, e);
	}
}

static bool show_va_arg_call(struct code_buffer *out, struct expression *e)
{
	struct expression *argument;
	struct expression *va_list = NULL;
	struct expression *type = NULL;
	struct symbol *cast_type;
	struct ident *ident;
	int count = 0;

	if (!e->fn || e->fn->type != EXPR_SYMBOL)
		return false;
	ident = e->fn->symbol ? e->fn->symbol->ident : e->fn->symbol_name;
	if (!ident || strcmp(show_ident(ident), "__extract_va_arg") != 0)
		return false;
	FOR_EACH_PTR(e->args, argument) {
		if (count == 0)
			va_list = argument;
		else if (count == 1)
			type = argument;
		count++;
	} END_FOR_EACH_PTR(argument);
	if (count != 2 || !type || type->type != EXPR_CAST)
		return false;
	cast_type = strip_type_wrappers(type->cast_type);
	if (!type_is_ptr(cast_type))
		return false;
	cast_type = show_base_type(cast_type);
	if (!cast_type)
		return false;
	append(out, "__builtin_va_arg(");
	expr(out, va_list);
	append(out, ", ");
	show_c_typename(out, cast_type);
	append(out, ")");
	return true;
}

static bool is_binary_expression(struct expression *e)
{
	if (!e)
		return false;
	if (e->type == EXPR_COMPARE &&
	    e->left && e->left->type == EXPR_TYPE &&
	    e->right && e->right->type == EXPR_TYPE)
		return false;
	return e->type == EXPR_BINOP || e->type == EXPR_COMPARE ||
	       e->type == EXPR_LOGICAL || e->type == EXPR_COMMA ||
	       e->type == EXPR_ASSIGNMENT;
}

static int binary_precedence(struct expression *e)
{
	switch (e->type) {
	case EXPR_COMMA:
		return 1;
	case EXPR_ASSIGNMENT:
		return 2;
	case EXPR_LOGICAL:
		if (e->op == SPECIAL_LOGICAL_OR)
			return 4;
		if (e->op == SPECIAL_LOGICAL_AND)
			return 5;
		return 0;
	case EXPR_COMPARE:
		switch (e->op) {
		case SPECIAL_EQUAL:
		case SPECIAL_NOTEQUAL:
			return 9;
		case '<': case '>':
		case SPECIAL_LTE: case SPECIAL_GTE:
		case SPECIAL_UNSIGNED_LT: case SPECIAL_UNSIGNED_GT:
		case SPECIAL_UNSIGNED_LTE: case SPECIAL_UNSIGNED_GTE:
			return 10;
		}
		return 0;
	case EXPR_BINOP:
		switch (e->op) {
		case '|':
			return 6;
		case '^':
			return 7;
		case '&':
			return 8;
		case SPECIAL_LEFTSHIFT:
		case SPECIAL_RIGHTSHIFT:
			return 11;
		case '+': case '-':
			return 12;
		case '*': case '/': case '%':
			return 13;
		}
		return 0;
	default:
		return 0;
	}
}

static bool is_bitwise_binop(struct expression *e)
{
	return e->type == EXPR_BINOP &&
	       (e->op == '&' || e->op == '|' || e->op == '^');
}

/* Keep the parentheses which gcc -Wparentheses asks for. */
static bool parentheses_avoid_warning(struct expression *parent,
				      struct expression *child)
{
	if (parent->type == EXPR_LOGICAL && parent->op == SPECIAL_LOGICAL_OR &&
	    child->type == EXPR_LOGICAL && child->op == SPECIAL_LOGICAL_AND)
		return true;
	if (parent->type == EXPR_BINOP &&
	    (parent->op == SPECIAL_LEFTSHIFT || parent->op == SPECIAL_RIGHTSHIFT) &&
	    child->type == EXPR_BINOP && (child->op == '+' || child->op == '-'))
		return true;
	if (is_bitwise_binop(parent) &&
	    (child->type == EXPR_BINOP || child->type == EXPR_COMPARE) &&
	    child->op != parent->op)
		return true;
	if (parent->type == EXPR_COMPARE && child->type == EXPR_COMPARE)
		return true;
	return false;
}

static bool binary_parent_needs_parenthesis(struct expression *parent,
					    struct expression *child)
{
	int parent_precedence = binary_precedence(parent);
	int child_precedence = binary_precedence(child);

	if (!parent_precedence || !child_precedence)
		return true;
	if (child_precedence < parent_precedence)
		return true;
	if (child_precedence == parent_precedence) {
		/* Assignments group right to left, everything else left to right. */
		if (parent->type == EXPR_ASSIGNMENT)
			return child != parent->right;
		return child != parent->left;
	}
	return parentheses_avoid_warning(parent, child);
}

/* The "foo + bar" which show_indexed_dereference() prints as "foo[bar]". */
static struct expression *subscript_binop(struct expression *e)
{
	while (e && e->type == EXPR_PREOP && e->op == '(')
		e = e->unop;
	if (!e || e->type != EXPR_BINOP || e->op != '+')
		return NULL;
	return e;
}

static struct expression *parent_subscript(struct expression *parent)
{
	struct expression *deref;

	switch (parent->type) {
	case EXPR_PREOP:
		if (parent->op == '*')
			return subscript_binop(parent->unop);
		deref = parent->unop;
		if (parent->op == '&' && deref && deref->type == EXPR_DEREF &&
		    deref->member && deref->deref &&
		    deref->deref->type == EXPR_PREOP && deref->deref->op == '*')
			return subscript_binop(deref->deref->unop);
		return NULL;
	case EXPR_DEREF:
		deref = parent->deref;
		if (!parent->member)
			return subscript_binop(deref);
		if (deref && deref->type == EXPR_PREOP && deref->op == '*')
			return subscript_binop(deref->unop);
		return NULL;
	case EXPR_CALL:
		deref = parent->fn;
		if (deref && deref->type == EXPR_PREOP && deref->op == '*' &&
		    deref->unop && deref->unop->type == EXPR_BINOP &&
		    deref->unop->op == '+')
			return deref->unop;
		return NULL;
	default:
		return NULL;
	}
}

static bool needs_parenthesis(struct expression *e)
{
	struct expression *child = e;
	struct expression *parent;
	struct expression *subscript;

	if (!is_binary_expression(e))
		return false;
	for (;;) {
		if (!child->parent)
			return true;
		/* A statement parent prints the expression on its own. */
		if (!(child->parent & 0x1UL))
			return false;
		parent = (struct expression *)(child->parent & ~0x1UL);
		if (parent->type != EXPR_IMPLIED_CAST)
			break;
		child = parent;
	}

	if (is_binary_expression(parent))
		return binary_parent_needs_parenthesis(parent, e);

	subscript = parent_subscript(parent);
	if (subscript && child == subscript->right)
		return false;
	if (subscript && child == subscript->left)
		return true;

	switch (parent->type) {
	case EXPR_PREOP:
		return parent->op != '(';
	case EXPR_CALL:
		return child == parent->fn || e->type == EXPR_COMMA;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		if (child == parent->cond_true)
			return e->type == EXPR_COMMA;
		return e->type == EXPR_COMMA || e->type == EXPR_ASSIGNMENT;
	case EXPR_INITIALIZER:
		return e->type == EXPR_COMMA;
	default:
		return true;
	}
}

static void expr(struct code_buffer *out, struct expression *e)
{
	struct expression *previous;
	struct expression *tmp;
	bool parenthesis;

	if (!e) {
		append(out, "/* null */");
		return;
	}
	previous = rendering_expression;
	if (!e->parent && previous)
		e->parent = (unsigned long)previous | 0x1UL;
	rendering_expression = e;
	switch (e->type) {
	case EXPR_VALUE:
		show_value(out, e);
		break;
	case EXPR_FVALUE:
		show_fvalue(out, e);
		break;
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
		if (e->op == '*' && show_indexed_dereference(out, e->unop)) {
			break;
		} else if (e->op == '&' && e->unop && e->unop->type == EXPR_DEREF &&
		    e->unop->member && e->unop->deref &&
		    e->unop->deref->type == EXPR_PREOP &&
		    e->unop->deref->op == '*') {
			append(out, "&");
			if (show_indexed_dereference(out, e->unop->deref->unop)) {
				append(out, ".%s", show_ident(e->unop->member));
			} else {
				expr(out, e->unop->deref->unop);
				append(out, "->%s", show_ident(e->unop->member));
			}
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
	case EXPR_COMMA:
	case EXPR_ASSIGNMENT:
		parenthesis = needs_parenthesis(e);
		if (parenthesis)
			append(out, "(");
		expr(out, e->left);
		append(out, " %s ", show_special(e->op));
		expr(out, e->right);
		if (parenthesis)
			append(out, ")");
		break;
	case EXPR_DEREF:
		if (!e->member && show_indexed_dereference(out, e->deref)) {
			break;
		}
		append(out, "(");
		if (e->member && e->deref && e->deref->type == EXPR_PREOP &&
		    e->deref->op == '*') {
			if (show_indexed_dereference(out, e->deref->unop)) {
				append(out, ".%s", show_ident(e->member));
			} else {
				expr(out, e->deref->unop);
				append(out, "->%s", show_ident(e->member));
			}
		} else {
			expr(out, e->deref);
			if (e->member)
				append(out, ".%s", show_ident(e->member));
		}
		append(out, ")");
		break;
	case EXPR_CALL:
		if (show_va_arg_call(out, e))
			break;
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
		append(out, "{\n");
		out->indent++;
		FOR_EACH_PTR(e->expr_list, tmp) {
			expr(out, tmp);
			append(out, ",\n");
		} END_FOR_EACH_PTR(tmp);
		out->indent--;
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

static struct symbol *declaration_anonymous_tag(struct symbol *s)
{
	return type_has_typeof(s) ? NULL : find_anonymous_tag(s);
}

static struct ident_list *alias_targets;
static bool alias_targets_collected;

static void collect_alias_targets(struct scope *scope)
{
	struct symbol *alias;
	struct ident *target;

	if (!scope)
		return;
	FOR_EACH_PTR(scope->symbols, alias) {
		if (!alias->alias || alias->alias->type != EXPR_STRING)
			continue;
		target = built_in_ident(alias->alias->string->data);
		add_ptr_list(&alias_targets, target);
	} END_FOR_EACH_PTR(alias);
}

static bool is_alias_target(struct symbol *s)
{
	if (!s->ident)
		return false;
	if (!alias_targets_collected) {
		/* Non-static aliases are bound in the global scope. */
		collect_alias_targets(file_scope);
		collect_alias_targets(global_scope);
		alias_targets_collected = true;
	}
	return lookup_ptr_list_entry((struct ptr_list *)alias_targets, s->ident);
}

static void declaration_suffix(struct code_buffer *out, struct symbol *s)
{
	show_asm_name(out, s);
	if (s->alias) {
		append(out, " __attribute__((alias(");
		expr(out, s->alias);
		append(out, "))) ");
	}
	/* GCC ignores "used" on the alias itself. */
	if (is_alias_target(s))
		append(out, " __attribute__((used))");
	if (symbol_initializer(s)) {
		append(out, " = ");
		expr(out, symbol_initializer(s));
	}
}

static void show_local_tag(struct code_buffer *out, struct symbol *tag)
{
	struct tag_output output = { .out = out };

	if (lookup_ptr_list_entry((struct ptr_list *)out->local_tags, tag))
		return;
	if (show_tag_definition(&output, tag))
		append(out, ";\n");
	add_symbol(&out->local_tags, tag);
}

static void declaration_body(struct code_buffer *out, struct symbol *s)
{
	struct symbol *anonymous_tag = declaration_anonymous_tag(s);
	struct symbol *tag = s;

	while (tag) {
		if (is_tag_type(tag) &&
		    tag->ident && tag_has_definition(tag) && tag->scope &&
		    !toplevel(tag->scope))
			break;
		if (tag->type == SYM_TYPEOF || tag->type == SYM_TYPEOF_UNQUAL)
			tag = typeof_base_type(tag);
		else
			tag = tag->ctype.base_type;
	}
	if (tag)
		show_local_tag(out, tag);

	if (anonymous_tag)
		show_anonymous_c_typename(out, s, anonymous_tag);
	else
		show_c_typename(out, s);

	declaration_suffix(out, s);
}

static void declaration(struct code_buffer *out, struct symbol *s)
{
	declaration_body(out, s);
	append(out, ";\n");
}

/*
 * Declarators which share an anonymous tag must stay in one declaration
 * or each one gets a distinct type.
 */
static void declarations(struct code_buffer *out, struct symbol_list *list)
{
	struct symbol *prev_tag = NULL;
	struct symbol *tag;
	struct symbol *s;

	FOR_EACH_PTR(list, s) {
		tag = declaration_anonymous_tag(s);
		if (tag && tag == prev_tag) {
			append(out, ", ");
			show_anonymous_declarator(out, s, tag);
			declaration_suffix(out, s);
			continue;
		}
		if (prev_tag)
			append(out, ";\n");
		if (tag) {
			declaration_body(out, s);
			prev_tag = tag;
		} else {
			declaration(out, s);
			prev_tag = NULL;
		}
	} END_FOR_EACH_PTR(s);
	if (prev_tag)
		append(out, ";\n");
}

/* The statement's own syntax separates "e" from the code around it. */
static void stmt_expr(struct code_buffer *out, struct statement *s,
		      struct expression *e)
{
	if (e && !e->parent)
		e->parent = (unsigned long)s;
	expr(out, e);
}

static void iterator_statement(struct code_buffer *out, struct statement *s)
{
	if (!s || s->type == STMT_NONE)
		return;
	if (s->type == STMT_EXPRESSION) {
		stmt_expr(out, s, s->expression);
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
		if (s->tag && tag_has_definition(s->tag))
			show_local_tag(out, s->tag);
		declarations(out, s->declaration);
		break;
	case STMT_EXPRESSION:
		stmt_expr(out, s, s->expression);
		append(out, ";\n");
		break;
	case STMT_RETURN:
		append(out, "return");
		if (s->ret_value) {
			append(out, " ");
			stmt_expr(out, s, s->ret_value);
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
		stmt_expr(out, s, s->switch_expression);
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
		stmt_expr(out, s, s->if_conditional);
		if (s->if_true && s->if_true->type != STMT_COMPOUND) {
			append(out, ")\n");
			out->indent++;
			stmt(out, s->if_true);
			out->indent--;
		} else {
			append(out, ") ");
			stmt(out, s->if_true);
		}
		if (s->if_false) {
			if (s->if_false->type == STMT_COMPOUND ||
			    s->if_false->type == STMT_IF) {
				append(out, "else ");
				stmt(out, s->if_false);
			} else {
				append(out, "else\n");
				out->indent++;
				stmt(out, s->if_false);
				out->indent--;
			}
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
				stmt_expr(out, s, s->iterator_pre_condition);
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
			stmt_expr(out, s, s->iterator_pre_condition);
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
			stmt_expr(out, s, s->iterator_post_condition);
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

static bool symbol_is_being_printed(struct code_buffer *out, struct symbol *s)
{
	return lookup_ptr_list_entry((struct ptr_list *)out->printing, s);
}

static struct symbol *function_definition(struct symbol *s)
{
	unsigned int i;

	if (s && s->definition && !declaration_has_typeof(s))
		return s->definition;
	for (i = 0; s && i < 8192; i++) {
		struct symbol *type = strip_type_wrappers(s);

		if (type && type->type == SYM_FN && !type_has_typeof(s) &&
		    (type->stmt || type->inline_stmt))
			return s;
		s = s->same_symbol;
	}
	return NULL;
}

static void push_printing_symbol(struct code_buffer *out, struct symbol *s)
{
	add_symbol(&out->printing, s);
}

static void pop_printing_symbol(struct code_buffer *out, struct symbol *s)
{
	if (last_ptr_list((struct ptr_list *)out->printing) == s)
		delete_ptr_list_last((struct ptr_list **)&out->printing);
}

static void show_symbol_declaration(struct code_buffer *out, struct symbol *s)
{
	struct symbol copy = *s;
	struct symbol *type = s->ctype.base_type;

	if (symbol_was_declared(s))
		return;
	/* An anonymous tag can only be emitted with its complete definition. */
	if (find_anonymous_tag(type))
		return;
	if (type && type->type == SYM_FN)
		show_function_forwards(out, type);
	if (!(copy.ctype.modifiers & MOD_STATIC))
		copy.ctype.modifiers |= MOD_EXTERN;
	show_c_typename(out, &copy);
	show_asm_name(out, &copy);
	append(out, ";\n");
	remember_declaration(s);
}

static void show_printing_declarations(struct code_buffer *out)
{
	struct symbol *symbol;

	FOR_EACH_PTR(out->printing, symbol) {
		show_symbol_declaration(out, symbol);
	} END_FOR_EACH_PTR(symbol);
}

static bool is_incomplete_array_declaration(struct symbol *symbol)
{
	struct symbol *type;

	for (type = symbol->ctype.base_type; type; type = show_base_type(type)) {
		if (type_is_array(type))
			return !type->array_size;
	}
	return false;
}

static struct symbol *initialized_definition(struct symbol *symbol)
{
	struct symbol *definition;

	if (!symbol->ident || !toplevel(symbol->scope))
		return NULL;
	definition = lookup_symbol(symbol->ident, symbol->namespace);
	if (!definition || definition == symbol ||
	    !find_symbol_initializer(definition))
		return NULL;
	return definition;
}

static void sym(struct code_buffer *out, struct symbol *s)
{
	struct symbol *definition;
	struct symbol *type;

	if (!s) {
		append(out, "/* null symbol */");
		return;
	}
	definition = function_definition(s);
	if (definition)
		s = definition;
	if (!symbol_initializer(s)) {
		definition = initialized_definition(s);
		if (definition)
			s = definition;
	}
	if (symbol_was_defined(s))
		return;
	type = s->ctype.base_type;
	if ((!type || type->type != SYM_FN) && !symbol_initializer(s) &&
	    (s->ctype.modifiers & MOD_EXTERN) && symbol_was_declared(s))
		return;
	if (is_incomplete_array_declaration(s) && !symbol_initializer(s) &&
	    symbol_was_declared(s))
		return;
	if (symbol_is_being_printed(out, s)) {
		show_printing_declarations(out);
		return;
	}
	push_printing_symbol(out, s);

	if (type && type->type == SYM_FN && !declaration_has_typeof(s)) {
		show_function(out, s);
		goto out;
	}

	show_initializer_dependencies(out, s);
	if (!(s->ctype.modifiers & MOD_EXTERN) || !symbol_was_declared(s))
		declaration(out, s);
	/* An initializer completes an array with an omitted bound. */
	if ((s->ctype.modifiers & MOD_EXTERN) ||
	    (is_incomplete_array_declaration(s) && !symbol_initializer(s)))
		remember_declaration(s);
	else
		remember_definition(s);
out:
	pop_printing_symbol(out, s);
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

static struct symbol *anonymous_tag_owner(struct symbol_list *symbols,
					  struct symbol *type,
					  struct symbol *anonymous_tag)
{
	struct symbol *entry;

	FOR_EACH_PTR(symbols, entry) {
		if (entry != type && !is_tag_type(entry) &&
		    entry->namespace == NS_SYMBOL &&
		    declaration_anonymous_tag(entry) == anonymous_tag)
			return entry;
	} END_FOR_EACH_PTR(entry);
	return NULL;
}

/*
 * File scope declarators which share an anonymous tag are printed as
 * separate declarations.  Name the tag through the symbol which already
 * defined it, such as "__typeof__((*ptr))", so the types stay the same.
 */
static bool show_anonymous_tag_typeof(struct code_buffer *out,
				      struct symbol *type,
				      struct symbol *anonymous_tag)
{
	struct symbol *owner;
	struct symbol *base;
	char ops[16];
	int nr = 0;
	int i;

	owner = anonymous_tag_owner(printed.definitions, type, anonymous_tag);
	if (!owner)
		owner = anonymous_tag_owner(printed.declarations, type,
					    anonymous_tag);
	if (!owner || !owner->ident)
		return false;
	for (base = show_base_type(owner); base && base != anonymous_tag;
	     base = show_base_type(base)) {
		if (base->type == SYM_NODE)
			continue;
		if ((base->type != SYM_PTR && base->type != SYM_ARRAY) ||
		    nr == ARRAY_SIZE(ops))
			return false;
		ops[nr++] = base->type == SYM_PTR ? '*' : '[';
	}
	if (base != anonymous_tag)
		return false;
	append(out, "__typeof__(");
	for (i = nr - 1; i >= 0; i--)
		append(out, ops[i] == '*' ? "(*" : "(");
	append(out, "%s", show_ident(owner->ident));
	for (i = 0; i < nr; i++)
		append(out, ops[i] == '*' ? ")" : ")[0]");
	append(out, ")");
	return true;
}

static void show_anonymous_c_typename(struct code_buffer *out,
				      struct symbol *type,
				      struct symbol *anonymous_tag)
{
	struct tag_output output = { .out = out };
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
		if (!show_anonymous_tag_typeof(out, type, anonymous_tag))
			show_tag_definition(&output, anonymous_tag);
		if (type->ident)
			append(out, " %s", show_ident(type->ident));
		return;
	}
	*tag = '\0';
	append(out, "%s", buf);
	if (!show_anonymous_tag_typeof(out, type, anonymous_tag))
		show_tag_definition(&output, anonymous_tag);
	append(out, "%s", tag + strlen(placeholder));
}

/* Print only the declarator, such as "*ptr2", for a shared anonymous tag. */
static void show_anonymous_declarator(struct code_buffer *out,
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
		if (type->ident)
			append(out, "%s", show_ident(type->ident));
		return;
	}
	tag += strlen(placeholder);
	if (*tag == ' ')
		tag++;
	append(out, "%s", tag);
}

static void show_function(struct code_buffer *out, struct symbol *s)
{
	struct symbol *type = s->ctype.base_type;
	struct statement *body;
	bool has_definition, asm_goto;

	if (!type)
		return;
	body = type->stmt ? type->stmt : type->inline_stmt;
	has_definition = body != NULL;
	asm_goto = statement_has_asm_goto(body);
	if (symbol_was_defined(s))
		return;
	if (!has_definition &&
	    symbol_was_declared(s))
		return;

	show_function_forwards(out, type);
	show_function_dependencies(out, s);
	if (has_definition) {
		if (asm_goto || (s->ctype.modifiers & MOD_EXTERN)) {
			struct symbol copy = *s;

			if (asm_goto)
				copy.ctype.modifiers |= MOD_ALWAYS_INLINE;
			copy.ctype.modifiers &= ~MOD_EXTERN;
			show_c_typename(out, &copy);
		} else {
			show_c_typename(out, s);
		}
		append(out, "\n");
		function_body(out, body);
		remember_definition(s);
	} else {
		show_c_typename(out, s);
		append(out, ";\n");
		remember_declaration(s);
	}
}

/*
 * An __aligned() member, or one declared with an aligned typedef, keeps
 * its alignment only in the member's ctype.  Lowering the alignment of
 * a member needs packed as well.
 */
static void show_member_alignment(struct code_buffer *out, struct symbol *member)
{
	struct symbol *base_type = member->ctype.base_type;
	unsigned long alignment = member->ctype.alignment;

	if (!base_type || base_type->type == SYM_BITFIELD || !alignment ||
	    !base_type->ctype.alignment ||
	    alignment == base_type->ctype.alignment)
		return;
	if (alignment < base_type->ctype.alignment)
		append(out, " __attribute__((packed, aligned(%lu)))", alignment);
	else
		append(out, " __attribute__((aligned(%lu)))", alignment);
}

static void show_struct_definition(struct tag_output *output, struct symbol *type)
{
	struct code_buffer *out = output->out;
	struct symbol *member;
	struct symbol *anonymous_tag;
	struct symbol *previous_tag = NULL;

	show_type_members(output, type);
	if (!type->ident) {
		append(out, "%s {\n", tag_type_name(type));
	} else if (is_typedef_tag(type)) {
		append(out, "typedef %s%s%s {\n", tag_type_name(type),
		       type->namespace == NS_STRUCT ? " " : "",
		       type->namespace == NS_STRUCT ? show_ident(type->ident) : "");
	} else {
		append(out, "%s %s {\n", tag_type_name(type), show_ident(type->ident));
	}
	out->indent++;
	FOR_EACH_PTR(type->symbol_list, member) {
		anonymous_tag = find_anonymous_tag(member);
		/* "struct { ... } a, b;" members must share one type. */
		if (anonymous_tag && anonymous_tag == previous_tag) {
			append(out, ", ");
			show_anonymous_declarator(out, member, anonymous_tag);
			show_member_alignment(out, member);
			continue;
		}
		if (previous_tag)
			append(out, ";\n");
		previous_tag = anonymous_tag;
		if (anonymous_tag) {
			show_anonymous_c_typename(out, member, anonymous_tag);
			show_member_alignment(out, member);
			continue;
		}
		show_c_typename(out, member);
		show_member_alignment(out, member);
		append(out, ";\n");
	} END_FOR_EACH_PTR(member);
	if (previous_tag)
		append(out, ";\n");
	out->indent--;
	append(out, "}");
	if (type->packed)
		append(out, " __attribute__((__packed__))");
	if (is_typedef_tag(type)) {
		append(out, " %s", show_ident(type->ident));
		if (type->transparent_union)
			append(out, " __attribute__((__transparent_union__))");
	}
	if (type->ident)
		remember_definition(type);
}

static void show_enum_definition(struct tag_output *output, struct symbol *type)
{
	struct code_buffer *out = output->out;
	struct symbol *member;

	if (!type->ident)
		append(out, "enum {\n");
	else if (is_typedef_tag(type))
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
		append(out, "} %s", show_ident(type->ident));
	else
		append(out, "}");
	if (type->ident)
		remember_definition(type);
}

static bool show_tag_definition(struct tag_output *output, struct symbol *type)
{
	struct symbol *typedef_symbol;

	if (is_typedef_tag(type)) {
		typedef_symbol = lookup_symbol(type->ident, NS_TYPEDEF);
		if (typedef_symbol &&
		    tag_was_defined(typedef_symbol))
			return false;
		if (typedef_symbol)
			remember_definition(typedef_symbol);
	}
	if (type->type == SYM_ENUM)
		show_enum_definition(output, type);
	else
		show_struct_definition(output, type);
	/* A local tag nested in another definition must not be printed twice. */
	if (type->scope && !toplevel(type->scope) &&
	    !lookup_ptr_list_entry((struct ptr_list *)output->out->local_tags,
				   type))
		add_symbol(&output->out->local_tags, type);
	return true;
}

static void show_typedef_definition(struct code_buffer *out, struct symbol *type)
{
	struct symbol copy;
	struct symbol *base_type;

	if ((type->ident && strcmp(show_ident(type->ident), "va_list") == 0) ||
	    type->ctype.base_type == &ptr_ctype) {
		append(out, "typedef __builtin_va_list %s;\n",
		       show_ident(type->ident));
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
		if (!tag_was_defined(tag) &&
		    show_tag_definition(output, tag))
			append(output->out, ";\n");
		return;
	}
	if (symbol->namespace != NS_TYPEDEF || !symbol->ident ||
	    lookup_symbol(symbol->ident, NS_TYPEDEF) != symbol)
		return;
	if (tag_was_defined(symbol))
		return;
	remember_definition(symbol);
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
	free_printed();
	free_dependencies();

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

/* Forget what was printed so the next output file starts empty. */
void show_code_reset(void)
{
	free_printed();
	free_dependencies();
	free_ptr_list(&alias_targets);
	alias_targets_collected = false;
}

void show_sym(struct symbol *s, char *buf, size_t len)
{
	struct code_buffer out = { .buf = buf, .len = len, .line_start = true };

	if (!len)
		return;
	buf[0] = '\0';
	sym(&out, s);
}
