#ifndef EVALUATE_H
#define EVALUATE_H

struct expression;
struct statement;
struct symbol;
struct symbol_list;

struct symbol *evaluate_expression(struct expression *expr);
struct symbol *evaluate_statement(struct statement *stmt);
void evaluate_symbol_list(struct symbol_list *list);

#endif
