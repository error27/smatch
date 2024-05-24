/*
 * Copyright (C) 2022 Oracle.
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

static unsigned long warned;
static unsigned long irq;
static unsigned long irq_save;

static const char *irq_funcs[] = {
	"spin_lock_irq",
	"spin_unlock_irq",
	"_spin_lock_irq",
	"_spin_unlock_irq",
	"__spin_lock_irq",
	"__spin_unlock_irq",
	"_raw_spin_lock_irq",
	"_raw_spin_unlock_irq",
	"__raw_spin_unlock_irq",
	"spin_trylock_irq",
	"read_lock_irq",
	"read_unlock_irq",
	"_read_lock_irq",
	"_read_unlock_irq",
	"__read_lock_irq",
	"__read_unlock_irq",
	"_raw_read_unlock_irq",
	"_raw_read_lock_irq",
	"__raw_write_unlock_irq",
	"__raw_write_unlock_irq",
	"write_lock_irq",
	"write_unlock_irq",
	"_write_lock_irq",
	"_write_unlock_irq",
	"__write_lock_irq",
	"__write_unlock_irq",
	"_raw_write_unlock_irq",
	"_raw_write_lock_irq",
	"raw_local_irq_disable",
	"raw_local_irq_enable",
	"spin_lock_irq",
	"spin_unlock_irq",
	"_spin_lock_irq",
	"_spin_unlock_irq",
	"__spin_lock_irq",
	"__spin_unlock_irq",
	"_raw_spin_lock_irq",
	"_raw_spin_unlock_irq",
	"__raw_spin_unlock_irq",
	"spin_trylock_irq",
	"read_lock_irq",
	"read_unlock_irq",
	"_read_lock_irq",
	"_read_unlock_irq",
	"__read_lock_irq",
	"_raw_read_lock_irq",
	"__read_unlock_irq",
	"_raw_read_unlock_irq",
	"write_lock_irq",
	"write_unlock_irq",
	"_write_lock_irq",
	"_write_unlock_irq",
	"__write_lock_irq",
	"__write_unlock_irq",
	"_raw_write_lock_irq",
	"_raw_write_unlock_irq",
};

static const char *irqsave_funcs[] = {
	"spin_lock_irqsave",
	"_spin_lock_irqsave",
	"__spin_lock_irqsave",
	"_raw_spin_lock_irqsave",
	"__raw_spin_lock_irqsave",
	"spin_lock_irqsave_nested",
	"_spin_lock_irqsave_nested",
	"__spin_lock_irqsave_nested",
	"_raw_spin_lock_irqsave_nested",
	"spin_trylock_irqsave",
	"read_lock_irqsave",
	"_read_lock_irqsave",
	"__read_lock_irqsave",
	"_raw_read_lock_irqsave",
	"_raw_read_lock_irqsave",
	"_raw_write_lock_irqsave",
	"_raw_write_lock_irqsave",
	"write_lock_irqsave",
	"_write_lock_irqsave",
	"__write_lock_irqsave",
	"arch_local_irq_save",
	"__raw_local_irq_save",
	"spin_lock_irqsave_nested",
	"spin_lock_irqsave",
	"_spin_lock_irqsave_nested",
	"_spin_lock_irqsave",
	"_spin_lock_irqsave",
	"__spin_lock_irqsave_nested",
	"__spin_lock_irqsave",
	"_raw_spin_lock_irqsave",
	"_raw_spin_lock_irqsave",
	"__raw_spin_lock_irqsave",
	"_raw_spin_lock_irqsave_nested",
	"spin_trylock_irqsave",
	"read_lock_irqsave",
	"read_lock_irqsave",
	"_read_lock_irqsave",
	"_read_lock_irqsave",
	"__read_lock_irqsave",
	"write_lock_irqsave",
	"write_lock_irqsave",
	"_write_lock_irqsave",
	"_write_lock_irqsave",
	"__write_lock_irqsave",
	"spin_unlock_irqrestore",
	"_spin_unlock_irqrestore",
	"__spin_unlock_irqrestore",
	"_raw_spin_unlock_irqrestore",
	"__raw_spin_unlock_irqrestore",
	"read_unlock_irqrestore",
	"_read_unlock_irqrestore",
	"__read_unlock_irqrestore",
	"_raw_read_unlock_irqrestore",
	"_raw_write_unlock_irqrestore",
	"__raw_write_unlock_irqrestore",
	"write_unlock_irqrestore",
	"_write_unlock_irqrestore",
	"__write_unlock_irqrestore",
};

static void irq_hook(const char *fn, struct expression *expr, void *_param)
{
	if (warned)
		return;
	if (irq_save) {
		sm_warning("mixing irqsave and irq");
		warned = true;
	}
	irq = true;
}

static void irqsave_hook(const char *fn, struct expression *expr, void *_param)
{
	char *macro;

	if (warned)
		return;
	macro = get_macro_name(expr->pos);
	if (macro && !strstr(macro, "irq"))
		return;
	if (irq) {
		sm_warning("mixing irq and irqsave");
		warned = true;
	}
	irq_save = true;
}

void check_mixing_irq_and_irqsave(int id)
{
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_data(&warned);
	add_function_data(&irq);
	add_function_data(&irq_save);

	for (i = 0; i < ARRAY_SIZE(irq_funcs); i++)
		add_function_hook(irq_funcs[i], irq_hook, NULL);

	for (i = 0; i < ARRAY_SIZE(irqsave_funcs); i++)
		add_function_hook(irqsave_funcs[i], irqsave_hook, NULL);


}
