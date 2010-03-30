
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "ast-inspect.h"

static inline void inspect_ptr_list(AstNode *node, const char *name, void (*inspect)(AstNode *))
{
	struct ptr_list *ptrlist = node->ptr;
	void *ptr;
	int i = 0;

	node->text = g_strdup_printf("%s %s:", node->text, name);
	FOR_EACH_PTR(ptrlist, ptr) {
		char *index = g_strdup_printf("%d: ", i++);
		ast_append_child(node, index, ptr, inspect);
	} END_FOR_EACH_PTR(ptr);
}


static const char *statement_type_name(enum statement_type type)
{
	static const char *statement_type_name[] = {
		[STMT_NONE] = "STMT_NONE",
		[STMT_DECLARATION] = "STMT_DECLARATION",
		[STMT_EXPRESSION] = "STMT_EXPRESSION",
		[STMT_COMPOUND] = "STMT_COMPOUND",
		[STMT_IF] = "STMT_IF",
		[STMT_RETURN] = "STMT_RETURN",
		[STMT_CASE] = "STMT_CASE",
		[STMT_SWITCH] = "STMT_SWITCH",
		[STMT_ITERATOR] = "STMT_ITERATOR",
		[STMT_LABEL] = "STMT_LABEL",
		[STMT_GOTO] = "STMT_GOTO",
		[STMT_ASM] = "STMT_ASM",
		[STMT_CONTEXT] = "STMT_CONTEXT",
		[STMT_RANGE] = "STMT_RANGE",
	};
	return statement_type_name[type] ?: "UNKNOWN_STATEMENT_TYPE";
}


void inspect_statement(AstNode *node)
{
	struct statement *stmt = node->ptr;
	node->text = g_strdup_printf("%s %s:", node->text, statement_type_name(stmt->type));
	switch (stmt->type) {
		case STMT_COMPOUND:
			ast_append_child(node, "stmts:", stmt->stmts, inspect_statement_list);
			break;
		case STMT_IF:
			ast_append_child(node, "if_true:", stmt->if_true, inspect_statement);
			ast_append_child(node, "if_false:", stmt->if_false, inspect_statement);
		default:
			break;
	}
}


void inspect_statement_list(AstNode *node)
{
	inspect_ptr_list(node, "statement_list", inspect_statement);
}


static const char *symbol_type_name(enum type type)
{
	static const char *type_name[] = {
		[SYM_UNINITIALIZED] = "SYM_UNINITIALIZED",
		[SYM_PREPROCESSOR] = "SYM_PREPROCESSOR",
		[SYM_BASETYPE] = "SYM_BASETYPE",
		[SYM_NODE] = "SYM_NODE",
		[SYM_PTR] = "SYM_PTR",
		[SYM_FN] = "SYM_FN",
		[SYM_ARRAY] = "SYM_ARRAY",
		[SYM_STRUCT] = "SYM_STRUCT",
		[SYM_UNION] = "SYM_UNION",
		[SYM_ENUM] = "SYM_ENUM",
		[SYM_TYPEDEF] = "SYM_TYPEDEF",
		[SYM_TYPEOF] = "SYM_TYPEOF",
		[SYM_MEMBER] = "SYM_MEMBER",
		[SYM_BITFIELD] = "SYM_BITFIELD",
		[SYM_LABEL] = "SYM_LABEL",
		[SYM_RESTRICT] = "SYM_RESTRICT",
		[SYM_FOULED] = "SYM_FOULED",
		[SYM_KEYWORD] = "SYM_KEYWORD",
		[SYM_BAD] = "SYM_BAD",
	};
	return type_name[type] ?: "UNKNOWN_TYPE";
}


void inspect_symbol(AstNode *node)
{
	struct symbol *sym = node->ptr;
	node->text = g_strdup_printf("%s %s: %s", node->text, symbol_type_name(sym->type),
				      show_ident(sym->ident));
	ast_append_child(node, "ctype.base_type:", sym->ctype.base_type,inspect_symbol);

	switch (sym->type) {
		case SYM_FN:
			ast_append_child(node, "stmt:", sym->stmt, inspect_statement);
			break;
		default:
			break;
	}
}


void inspect_symbol_list(AstNode *node)
{
	inspect_ptr_list(node, "symbol_list", inspect_symbol);
}

