/*
 * Copyright (C) 2016 Oracle.
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
 * What we're doing here is saving all the possible values for static variables.
 * Later on we might do globals as well.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;
static struct stree *vals;

static int save_rl(void *_rl, int argc, char **argv, char **azColName)
{
	unsigned long *rl = _rl;

	*rl = strtoul(argv[0], NULL, 10);
	return 0;
}

static struct range_list *select_orig_rl(mtag_t tag, const char *data_name)
{
	struct range_list *rl = NULL;

	mem_sql(&save_rl, &rl, "select value from mtag_data where tag = %lld and data = '%s';",
		tag, data_name);
	return rl;
}

static int is_kernel_param(const char *name)
{
	struct sm_state *tmp;
	char buf[256];

	/*
	 * I'm ignoring these because otherwise Smatch thinks that kernel
	 * parameters are always set to the default.
	 *
	 */

	if (option_project != PROJ_KERNEL)
		return 0;

	snprintf(buf, sizeof(buf), "__param_%s.arg", name);

	FOR_EACH_SM(vals, tmp) {
		if (strcmp(tmp->name, buf) == 0)
			return 1;
	} END_FOR_EACH_SM(tmp);

	return 0;
}

void insert_mtag_data(mtag_t tag, const char *data_name, int offset, struct range_list *rl)
{
	rl = clone_rl_permanent(rl);

	mem_sql(NULL, NULL, "insert into mtag_data values (%lld, '%s', %d, %d, '%lu');",
		tag, data_name, offset, DATA_VALUE, (unsigned long)rl);
}

static void extra_mod_hook(const char *name, struct symbol *sym, struct expression *expr, struct smatch_state *state)
{
	struct range_list *orig, *new;
	char *data_name;
	mtag_t tag;
	int offset;

	if (is_kernel_param(name))
		return;
	if (!expr_to_mtag_name_offset(expr, &tag, &data_name, &offset))
		return;

	orig = select_orig_rl(tag, data_name);
	new = rl_union(orig, estate_rl(state));
	insert_mtag_data(tag, data_name, offset, new);
}

static int save_mtag_data(void *_unused, int argc, char **argv, char **azColName)
{
	struct range_list *rl;

	if (argc != 5) {
		sm_msg("Error saving mtag data");
		return 0;
	}

	rl = (struct range_list *)strtoul(argv[4], NULL, 10);

	if (option_info) {
		sm_msg("SQL: insert into mtag_data values ('%s', '%s', '%s', '%s', '%s');",
		       argv[0], argv[1], argv[2], argv[3], show_rl(rl));
	}

	return 0;
}

static void match_end_file(struct symbol_list *sym_list)
{
	mem_sql(&save_mtag_data, NULL, "select * from mtag_data;");
}

struct db_info {
	struct symbol *type;
	struct range_list *rl;
};

static int get_vals(void *_db_info, int argc, char **argv, char **azColName)
{
	struct db_info *db_info = _db_info;
	struct range_list *tmp;

	str_to_rl(db_info->type, argv[0], &tmp);
	if (db_info->rl)
		db_info->rl = rl_union(db_info->rl, tmp);
	else
		db_info->rl = tmp;

	return 0;
}

int get_db_data_rl(struct expression *expr, struct range_list **rl)
{
	const char *name;
	struct db_info db_info = {};
	mtag_t tag;

	if (!get_toplevel_mtag(expr_to_sym(expr), &tag))
		return 0;

	name = get_mtag_name_expr(expr);
	if (!name)
		return 0;

	db_info.type = get_type(expr);
	if (!db_info.type)
		return 0;
	run_sql(get_vals, &db_info,
		"select value from mtag_data where tag = %lld and data = '%s' and type = %d;",
		tag, name, DATA_VALUE);
	if (!db_info.rl || is_whole_rl(db_info.rl))
		return 0;

	*rl = db_info.rl;
	return 1;
}

void register_mtag_data(int id)
{
	if (!option_info)
		return;

	my_id = id;

	add_extra_mod_hook(&extra_mod_hook);

	add_hook(&match_end_file, END_FILE_HOOK);
}

