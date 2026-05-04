/*
 * Copyright 2025 Linaro Ltd.
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

/*
 * This might be overthinking things.  I'm almost embarrassed to
 * explain what this is...  The problem is that there are some
 * false positives when we do:
 * int i = 0;
 * if (!IS_ENABLED(CONFIG_FOO))
 *    return;
 * i++;
 *
 * Smatch says "warn: iterator 'i' not incremented"
 *
 * We could just disable the warning any time when i is incremented
 * in dead code, but that would silence some warnings which are
 * real bugs like:
 * for (i = 0; i < 10; i++) {
 *   ...
 *   return;
 * }
 *
 * But why am I worried about that when that will be caught by the
 * unreachable code warning???  It's because I am a moron.  And that's
 * why I'm embarrassed to explain this code.
 *
 * Update: This didn't work how I intended it.  The "not incremented"
 * check relies on the modification hooks which relies on the cur_stree
 * which is empty in dead code.  So instead of that, I just check if
 * we are ENABLED() at the end of the function, which I think should
 * silence most of the false postives even though it's kind of bogus.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

static unsigned long disabled;

static void match_condition(struct expression *expr)
{
	sval_t sval;
	char *macro;

	if (!get_value(expr, &sval) || sval.value != 0)
		return;

	macro = get_macro_name(expr->pos);
	if (!macro || !strstr(macro, "ENABLED"))
		return;

	disabled = true;
}

static struct statement *prev;

static void match_stmt(struct statement *stmt)
{
	if (!disabled)
		return;
	if (!__path_is_null()) {
		if (prev)
			disabled = false;
		return;
	}
	disabled = true;
	if (!prev)
		prev = stmt;
}

bool is_NOT_ENABLED(void)
{
	return disabled;
}

void register_kernel_NOT_ENABLED(int id)
{
	my_id = id;

	add_function_data(&disabled);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_stmt, STMT_HOOK_AFTER);
}
