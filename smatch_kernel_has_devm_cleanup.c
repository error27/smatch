/*
 * Copyright (C) 2020 Oracle.
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
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(devm_action);

static void ignore_path(const char *fn, struct expression *expr, void *data)
{
	set_state(my_id, "path", NULL, &devm_action);
}

bool has_devm_cleanup(void)
{
	if (get_state(my_id, "path", NULL) == &devm_action)
		return true;
	return false;
}

void register_kernel_has_devm_cleanup(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_function_hook("devm_add_action_or_reset", &ignore_path, NULL);
	add_function_hook("__devm_add_action_or_reset", &ignore_path, NULL);
	add_function_hook("drmm_add_action", &ignore_path, NULL);
	add_function_hook("__drmm_add_action", &ignore_path, NULL);
	add_function_hook("drmm_add_action_or_reset", &ignore_path, NULL);
	add_function_hook("__drmm_add_action_or_reset", &ignore_path, NULL);
	add_function_hook("pcim_enable_device", &ignore_path, NULL);
	add_function_hook("pci_enable_device", &ignore_path, NULL);
	add_function_hook("put_device", &ignore_path, NULL);
	add_function_hook("component_match_add_release", &ignore_path, NULL);
}

