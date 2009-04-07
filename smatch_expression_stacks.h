DECLARE_PTR_LIST(expression_stack, struct expression);
void push_expression(struct expression_stack **estack, struct expression *expr);
struct expression *pop_expression(struct expression_stack **estack);
struct expression *top_expression(struct expression_stack *estack);
void free_expression_stack(struct expression_stack **estack);
