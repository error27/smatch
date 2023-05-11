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
#include "smatch_extra.h"

static int my_id;

/* These functions do not necessarily need to be checked */
static const char *safe_fns[] = {
	"exynos_drm_crtc_get_by_type",
	"mdp5_crtc_get_mixer",
	"mdp5_crtc_get_pipeline",
	"mtk_vdec_h264_get_ctrl_ptr",
	"nand_get_sdr_timings",
	"tc358746_get_format_by_code",
	"to_caam_req",
	"uverbs_attr_get",
	"uverbs_attr_get_alloced_ptr",
	"uverbs_attr_get_obj",
	"uverbs_attr_get_uobject",
};

static bool from_safe_fn(struct expression *expr)
{
	struct expression *prev;
	int i;

	prev = get_assigned_expr(expr);
	if (!prev)
		return false;
	if (prev->type != EXPR_CALL)
		return false;

	for (i = 0; i < ARRAY_SIZE(safe_fns); i++) {
		if (sym_name_is(safe_fns[i], prev->fn))
			return true;
	}
	return false;
}

static void deref_hook(struct expression *expr)
{
	char *name;

	if (!possible_err_ptr(expr))
		return;
	if (from_safe_fn(expr))
		return;

	name = expr_to_str(expr);
	sm_error("'%s' dereferencing possible ERR_PTR()", name);
	free_string(name);
}

void check_err_ptr_deref(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_dereference_hook(deref_hook);
}
