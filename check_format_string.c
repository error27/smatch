#include <string.h>
#include "smatch.h"
#include "smatch_extra.h"

static int my_id;

static void match_printf(const char *fn, struct expression *expr, void *unused)
{
	struct expression *format;
	
	format = get_argument_from_call_expr(expr->args, 0);
	if (format -> type != EXPR_STRING) 
	    sm_msg("warn: format strings should be constant to avoid format string vulnerabilities");

}

void check_format_string(int id)
{
	if (!option_spammy)
		return;
	my_id = id;
	add_function_hook("printf", &match_printf, (void *)0);
}
