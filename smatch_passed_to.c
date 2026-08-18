#include "smatch.h"

static int my_id;

struct smatch_state_data {
	struct sm_state *previous;
	struct expression *expr;
	int param;
};

static struct smatch_state *alloc_passed_to_state(const char *fn_name,
						  struct expression *expr,
						  int param,
						  struct sm_state *previous)
{
	struct smatch_state_data *data;
	struct smatch_state *state;

	state = __alloc_smatch_state(sizeof(*data));
	data = (struct smatch_state_data *)(state + 1);

	state->name = alloc_sname(fn_name);
	state->data = data;
	data->previous = previous;
	data->expr = expr;
	data->param = param;

	return state;
}

static void save_passed_to(const char *fn_name, struct expression *expr, int param,
			   struct expression *arg)
{
	struct smatch_state *state;
	struct sm_state *previous;
	struct symbol *sym;
	char *name;

	name = expr_to_var_sym(arg, &sym);
	if (!name || !sym)
		goto free;

	previous = get_sm_state(my_id, name, sym);
	state = alloc_passed_to_state(fn_name, expr, param, previous);
	set_state(my_id, name, sym, state);
free:
	free_string(name);
}

static void match_call(struct expression *expr)
{
	struct expression *arg;
	char *fn_name;
	int param = -1;

	if (sym_name_is(expr->fn, "__smatch_passed_to"))
		return;

	fn_name = expr_to_str(expr->fn);
	if (!fn_name)
		return;

	FOR_EACH_PTR(expr->args, arg) {
		save_passed_to(fn_name, expr, ++param, arg);
	} END_FOR_EACH_PTR(arg);

	free_string(fn_name);
}

static void print_passed_to_sm(struct sm_state *sm, const char *name,
			       struct state_list **printed)
{
	struct smatch_state_data *data;
	char *fn_name;

	if (!sm)
		return;
	if (lookup_ptr_list_entry((struct ptr_list *)*printed, sm))
		return;
	add_ptr_list(printed, sm);

	data = sm->state->data;
	if (!data) {
		print_passed_to_sm(sm->left, name, printed);
		print_passed_to_sm(sm->right, name, printed);
		return;
	}

	print_passed_to_sm(data->previous, name, printed);

	fn_name = expr_to_str(data->expr->fn);
	if (!fn_name)
		return;
	if (name)
		sm_msg("%s passed to %s $%d", name, fn_name, data->param);
	else
		sm_msg("passed to %s $%d", fn_name, data->param);
	free_string(fn_name);
}

void print_passed_to(struct expression *expr)
{
	struct state_list *printed = NULL;

	print_passed_to_sm(get_sm_state_expr(my_id, expr), NULL, &printed);
	free_slist(&printed);
}

static void print_passed_to_states(void)
{
	struct state_list *printed;
	struct sm_state *sm;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		printed = NULL;
		print_passed_to_sm(sm, sm->name, &printed);
		free_slist(&printed);
	} END_FOR_EACH_SM(sm);
}

void smatch_passed_to(int id)
{
	my_id = id;

	set_dynamic_states(my_id);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	register_ai_info(&print_passed_to_states);
}
