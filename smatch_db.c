/*
 * smatch/smatch_db.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <string.h>
#include <errno.h>
#include <sqlite3.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static sqlite3 *db;

struct def_callback {
	int hook_type;
	void (*callback)(const char *name, struct symbol *sym, char *value);
};
ALLOCATOR(def_callback, "definition db hook callbacks");
DECLARE_PTR_LIST(callback_list, struct def_callback);
static struct callback_list *callbacks;

void sql_exec(int (*callback)(void*, int, char**, char**), const char *sql)
{
	char *err = NULL;
	int rc;

	if (option_no_db || !db)
		return;

	rc = sqlite3_exec(db, sql, callback, 0, &err);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error #2: %s\n", err);
		exit(1);
	}
}

void add_definition_db_callback(void (*callback)(const char *name, struct symbol *sym, char *value), int type)
{
	struct def_callback *def_callback = __alloc_def_callback(0);

	def_callback->hook_type = type;
	def_callback->callback = callback;
	add_ptr_list(&callbacks, def_callback);
}

static unsigned long call_count;
static int db_count_callback(void *unused, int argc, char **argv, char **azColName)
{
	call_count = strtoul(argv[0], NULL, 10);
	return 0;
}

static int get_param(int param, char **name, struct symbol **sym)
{
	struct symbol *arg;
	int i;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		/*
		 * this is a temporary hack to work around a bug (I think in sparse?)
		 * 2.6.37-rc1:fs/reiserfs/journal.o
		 * If there is a function definition without parameter name found 
		 * after a function implementation then it causes a crash.
		 * int foo() {}
		 * int bar(char *);
		 */
		if (arg->ident->name < (char *)100)
			continue;
		if (i == param && arg->ident->name) {
			*name = arg->ident->name;
			*sym = arg;
			return TRUE;
		}
		i++;
	} END_FOR_EACH_PTR(arg);

	return FALSE;
}

static struct state_list *final_states;
static int prev_func_id = -1;
static int db_callback(void *unused, int argc, char **argv, char **azColName)
{
	int func_id;
	int type;
	unsigned long param;
	char *name;
	struct symbol *sym;
	struct def_callback *def_callback;

	if (argc != 4)
		return 0;

	func_id = atoi(argv[0]);
	type = atoi(argv[1]);
	errno = 0;
	param = strtoul(argv[2], NULL, 10);
	if (errno)
		return 0;

	if (prev_func_id == -1)
		prev_func_id = func_id;
	if (func_id != prev_func_id) {
		merge_slist(&final_states, __pop_fake_cur_slist());
		__push_fake_cur_slist();
		prev_func_id = func_id;
	}

	if (!get_param(param, &name, &sym))
		return 0;

	FOR_EACH_PTR(callbacks, def_callback) {
		if (def_callback->hook_type == type)
			def_callback->callback(name, sym, argv[3]);
	} END_FOR_EACH_PTR(def_callback);

	return 0;
}

static void match_data_from_db(struct symbol *sym)
{
	char sql_filter[1024];
	struct sm_state *sm;

	if (!sym || !sym->ident || !sym->ident->name)
		return;

	if (sym->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, 1024,
			 "file = '%s' and function = '%s' order by function_id;",
			 get_filename(), sym->ident->name);
	} else {
		snprintf(sql_filter, 1024,
			 "function = '%s' order by function_id;",
			 sym->ident->name);
	}

	call_count = 0;
	run_sql(db_count_callback, "select count(*) from caller_info where %s",
		 sql_filter);
	if (call_count == 0 || call_count > 100)
		return;

	prev_func_id = -1;
	__push_fake_cur_slist();
	run_sql(db_callback, "select function_id, type, parameter, value from caller_info"
		" where %s", sql_filter);
	merge_slist(&final_states, __pop_fake_cur_slist());

	FOR_EACH_PTR(final_states, sm) {
		__set_sm(sm);
	} END_FOR_EACH_PTR(sm);

	free_slist(&final_states);
}

void open_smatch_db(void)
{
#ifdef SQLITE_OPEN_READONLY
	int rc;

	if (option_no_db)
		return;

	rc = sqlite3_open_v2("smatch_db.sqlite", &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		option_no_db = 1;
		return;
	}
	return;
#else
	option_no_db = 1;
	return;
#endif
}

void register_definition_db_callbacks(int id)
{
#ifdef SQLITE_OPEN_READONLY
	add_hook(&match_data_from_db, FUNC_DEF_HOOK);
#endif
}
