/*
 * smatch/smatch_db.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <string.h>
#include <sqlite3.h>
#include "smatch.h"
#include "smatch_extra.h"

static sqlite3 *db;

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

void open_smatch_db(void)
{
#ifdef SQLITE_OPEN_READONLY
	int rc;

	if (option_no_db)
		return;

	rc = sqlite3_open_v2("smatch_db.sqlite", &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK)
#endif
		option_no_db = 1;
}
