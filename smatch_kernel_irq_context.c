/*
 * Copyright 2023 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

bool in_irq_context(void)
{
	if (has_possible_state(my_id, "irq_context", NULL, &true_state))
		return true;
	return false;
}

void clear_irq_context(void)
{
	struct smatch_state *state;

	state = get_state(my_id, "irq_context", NULL);
	if (!state)
		return;
	set_state(my_id, "irq_context", NULL, &undefined);
}

static int db_set_irq(void *_found, int argc, char **argv, char **azColName)
{
	int *found = _found;
	*found = true;
	return 0;
}

static bool is_irq_handler(void)
{
	int found = 0;

	if (!cur_func_sym)
		return false;

	run_sql(db_set_irq, &found,
		"select * from fn_data_link where file = 0x%llx and function = '%s' and static = %d and type = %d;",
		(cur_func_sym->ctype.modifiers & MOD_STATIC) ? get_base_file_id() : 0,
		cur_func_sym->ident->name,
		!!(cur_func_sym->ctype.modifiers & MOD_STATIC),
		IRQ_CONTEXT);

	return found;
}

static void match_declaration(struct symbol *sym)
{
	if (!is_irq_handler())
		return;

	set_state(my_id, "irq_context", NULL, &true_state);
}

static void match_request_irq(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	struct expression *handler;
	sval_t sval = int_zero;

	while (expr && expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (!expr || expr->type != EXPR_CALL)
		return;

	handler = get_argument_from_call_expr(expr->args, 1);
	if (!get_implied_value(handler, &sval))
		return;

	sql_insert_fn_data_link(handler, IRQ_CONTEXT, -1, "", "");
}

static void match_request_threaded_irq(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	struct expression *thread;

	while (expr && expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (!expr || expr->type != EXPR_CALL)
		return;

	thread = get_argument_from_call_expr(expr->args, 2);
	if (!expr_is_zero(thread))
		return;
	if (get_function() && strcmp(get_function(), "request_irq") == 0)
		return;

	sm_warning("why call request_threaded_irq() with a NULL thread");
}

static void match_in_irq(struct expression *expr)
{
	char *macro = get_macro_name(expr->pos);

	if (!macro || strcmp(macro, "in_irq") != 0)
		return;
	set_true_false_states(my_id, "irq_context", NULL, NULL, &undefined);
}

static bool is_ignored_fn(struct expression *expr)
{
	char *fn;
	bool ret = false;

	fn = get_fnptr_name(expr->fn);
	if (!fn)
		return false;

	if (strcmp(fn, "bus_for_each_drv") == 0 ||
	    strcmp(fn, "call_mmap") == 0 ||
	    strcmp(fn, "(struct comedi_subdevice)->cancel") == 0 ||
	    strcmp(fn, "(struct flexcop_device)->read_ibi_reg") == 0 ||
	    strcmp(fn, "(struct irqaction)->handler") == 0 ||  /* called from __handle_irq_event_percpu() */
	    strcmp(fn, "(struct spi_message)->complete") == 0 ||
	    strcmp(fn, "(struct usb_gadget_driver)->setup") == 0 ||
	    strcmp(fn, "(struct v4l2_subdev_core_ops)->interrupt_service_routine") == 0 ||
	    strcmp(fn, "call_mmap") == 0)
		ret = true;

	free_string(fn);
	return ret;
}

static void match_call_info(struct expression *expr)
{
	if (!in_irq_context())
		return;

	if (is_ignored_fn(expr))
		return;

	sql_insert_caller_info(expr, IRQ_CONTEXT, -1, "", is_irq_handler() ? "<- IRQ handler" : "");
}

static void select_call_info(const char *name, struct symbol *sym, char *key, char *value)
{
	set_state(my_id, "irq_context", NULL, &true_state);
}

void register_kernel_irq_context(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_function_param_key_hook("request_irq", &match_request_irq, 1, "$", NULL);
	add_function_param_key_hook("request_threaded_irq", &match_request_threaded_irq, 1, "$", NULL);
	add_hook(&match_in_irq, CONDITION_HOOK);

	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
	select_caller_info_hook(&select_call_info, IRQ_CONTEXT);
	add_hook(&match_declaration, DECLARATION_HOOK);
}
