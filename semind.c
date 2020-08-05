/*
 * semind - semantic indexer for C.
 *
 * Copyright (C) 2020  Alexey Gladkov
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <sqlite3.h>

#include "dissect.h"

#define U_DEF (0x100 << U_SHIFT)
#define SINDEX_DATABASE_VERSION 1

#define message(fmt, ...) semind_error(0, 0, (fmt), ##__VA_ARGS__)

static const char *progname;
static const char *semind_command = NULL;

// common options
static const char *semind_dbfile = "semind.sqlite";
static int semind_verbose = 0;
static char cwd[PATH_MAX];
static size_t n_cwd;

// 'add' command options
static struct string_list *semind_filelist = NULL;
static int semind_include_local_syms = 0;

struct semind_streams {
	sqlite3_int64 id;
};

static struct semind_streams *semind_streams = NULL;
static int semind_streams_nr = 0;

// 'search' command options
static int semind_search_modmask;
static int semind_search_modmask_defined = 0;
static int semind_search_kind = 0;
static char *semind_search_path = NULL;
static char *semind_search_symbol = NULL;
static const char *semind_search_format = "(%m) %f\t%l\t%c\t%C\t%s";

#define EXPLAIN_LOCATION 1
#define USAGE_BY_LOCATION 2
static int semind_search_by_location;
static char *semind_search_filename;
static int semind_search_line;
static int semind_search_column;

static sqlite3 *semind_db = NULL;
static sqlite3_stmt *lock_stmt = NULL;
static sqlite3_stmt *unlock_stmt = NULL;
static sqlite3_stmt *insert_rec_stmt = NULL;
static sqlite3_stmt *select_file_stmt = NULL;
static sqlite3_stmt *insert_file_stmt = NULL;
static sqlite3_stmt *delete_file_stmt = NULL;

struct command {
	const char *name;
	int dbflags;
	void (*parse_cmdline)(int argc, char **argv);
	void (*handler)(int argc, char **argv);
};

static void show_usage(void)
{
	if (semind_command)
		printf("Try '%s %s --help' for more information.\n",
		       progname, semind_command);
	else
		printf("Try '%s --help' for more information.\n",
		       progname);
	exit(1);
}

static void show_help(int ret)
{
	printf(
	    "Usage: %1$s [options]\n"
	    "   or: %1$s [options] add    [command options] [--] [compiler options] [files...]\n"
	    "   or: %1$s [options] rm     [command options] pattern\n"
	    "   or: %1$s [options] search [command options] pattern\n"
	    "\n"
	    "These are common %1$s commands used in various situations:\n"
	    "  add      Generate or updates semantic index file for c-source code;\n"
	    "  rm       Remove files from the index by pattern;\n"
	    "  search   Make index queries.\n"
	    "\n"
	    "Options:\n"
	    "  -D, --database=FILE    Specify database file (default: %2$s);\n"
	    "  -B, --basedir=DIR      Define project top directory (default is the current directory);\n"
	    "  -v, --verbose          Show information about what is being done;\n"
	    "  -h, --help             Show this text and exit.\n"
	    "\n"
	    "Environment:\n"
	    "  SINDEX_DATABASE        Database file location.\n"
	    "  SINDEX_BASEDIR         Project top directory.\n"
	    "\n"
	    "Report bugs to authors.\n"
	    "\n",
	    progname, semind_dbfile);
	exit(ret);
}

static void show_help_add(int ret)
{
	printf(
	    "Usage: %1$s add [options] [--] [compiler options] files...\n"
	    "\n"
	    "Utility creates or updates a symbol index.\n"
	    "\n"
	    "Options:\n"
	    "  --include-local-syms   Include into the index local symbols;\n"
	    "  -v, --verbose          Show information about what is being done;\n"
	    "  -h, --help             Show this text and exit.\n"
	    "\n"
	    "Report bugs to authors.\n"
	    "\n",
	    progname);
	exit(ret);

}

static void show_help_rm(int ret)
{
	printf(
	    "Usage: %1$s rm [options] pattern\n"
	    "\n"
	    "Utility removes source files from the index.\n"
	    "The pattern is a glob(7) wildcard pattern.\n"
	    "\n"
	    "Options:\n"
	    "  -v, --verbose          Show information about what is being done;\n"
	    "  -h, --help             Show this text and exit.\n"
	    "\n"
	    "Report bugs to authors.\n"
	    "\n",
	    progname);
	exit(ret);
}

static void show_help_search(int ret)
{
	printf(
	    "Usage: %1$s search [options] [pattern]\n"
	    "   or: %1$s search [options] (-e|-l) filename[:linenr[:column]]\n"
	    "\n"
	    "Utility searches information about symbol by pattern.\n"
	    "The pattern is a glob(7) wildcard pattern.\n"
	    "\n"
	    "Options:\n"
	    "  -f, --format=STRING    Specify an output format;\n"
	    "  -p, --path=PATTERN     Search symbols only in specified directories;\n"
	    "  -m, --mode=MODE        Search only the specified type of access;\n"
	    "  -k, --kind=KIND        Specify a kind of symbol;\n"
	    "  -e, --explain          Show what happens in the specified file position;\n"
	    "  -l, --location         Show usage of symbols from a specific file position;\n"
	    "  -v, --verbose          Show information about what is being done;\n"
	    "  -h, --help             Show this text and exit.\n"
	    "\n"
	    "The KIND can be one of the following: `s', `f', `v', `m'.\n"
	    "\n"
	    "Report bugs to authors.\n"
	    "\n",
	    progname);
	exit(ret);
}

static void semind_print_progname(void)
{
	fprintf(stderr, "%s: ", progname);
	if (semind_command)
		fprintf(stderr, "%s: ", semind_command);
}

static void semind_error(int status, int errnum, const char *fmt, ...)
{
	va_list ap;
	semind_print_progname();

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (errnum > 0)
		fprintf(stderr, ": %s", strerror(errnum));

	fprintf(stderr, "\n");

	if (status)
		exit(status);
}

static void set_search_modmask(const char *v)
{
	size_t n = strlen(v);

	if (n != 1 && n != 3)
		semind_error(1, 0, "the length of mode value must be 1 or 3: %s", v);

	semind_search_modmask_defined = 1;
	semind_search_modmask = 0;

	if (n == 1) {
		switch (v[0]) {
			case 'r': v = "rrr"; break;
			case 'w': v = "ww-"; break;
			case 'm': v = "mmm"; break;
			case '-': v = "---"; break;
			default: semind_error(1, 0, "unknown modificator: %s", v);
		}
	} else if (!strcmp(v, "def")) {
		semind_search_modmask = U_DEF;
		return;
	}

	static const int modes[] = {
		U_R_AOF, U_W_AOF, U_R_AOF | U_W_AOF,
		U_R_VAL, U_W_VAL, U_R_VAL | U_W_VAL,
		U_R_PTR, U_W_PTR, U_R_PTR | U_W_PTR,
	};

	for (int i = 0; i < 3; i++) {
		switch (v[i]) {
			case 'r': semind_search_modmask |= modes[i * 3];     break;
			case 'w': semind_search_modmask |= modes[i * 3 + 1]; break;
			case 'm': semind_search_modmask |= modes[i * 3 + 2]; break;
			case '-': break;
			default:  semind_error(1, 0,
			                "unknown modificator in the mode value"
			                " (`r', `w', `m' or `-' expected): %c", v[i]);
		}
	}
}

static void parse_cmdline(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "database", required_argument, NULL, 'D' },
		{ "basedir", required_argument, NULL, 'B' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL }
	};
	int c;
	char *basedir = getenv("SINDEX_BASEDIR");
	char *env;

	if ((env = getenv("SINDEX_DATABASE")) != NULL)
		semind_dbfile = env;

	while ((c = getopt_long(argc, argv, "+B:D:vh", long_options, NULL)) != -1) {
		switch (c) {
			case 'D':
				semind_dbfile = optarg;
				break;
			case 'B':
				basedir = optarg;
				break;
			case 'v':
				semind_verbose++;
				break;
			case 'h':
				show_help(0);
		}
	}

	if (optind == argc) {
		message("command required");
		show_usage();
	}

	if (basedir) {
		if (!realpath(basedir, cwd))
			semind_error(1, errno, "unable to get project base directory");
		n_cwd = strlen(cwd);
	}
}

static void parse_cmdline_add(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "include-local-syms", no_argument, NULL, 1 },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL }
	};
	int c;

	opterr = 0;

	while ((c = getopt_long(argc, argv, "+vh", long_options, NULL)) != -1) {
		switch (c) {
			case 1:
				semind_include_local_syms = 1;
				break;
			case 'v':
				semind_verbose++;
				break;
			case 'h':
				show_help_add(0);
			case '?':
				goto done;
		}
	}
done:
	if (optind == argc) {
		message("more arguments required");
		show_usage();
	}

	// enforce tabstop
	tabstop = 1;

	// step back since sparse_initialize will ignore argv[0].
	optind--;

	sparse_initialize(argc - optind, argv + optind, &semind_filelist);
}

static void parse_cmdline_rm(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL }
	};
	int c;

	while ((c = getopt_long(argc, argv, "+vh", long_options, NULL)) != -1) {
		switch (c) {
			case 'v':
				semind_verbose++;
				break;
			case 'h':
				show_help_rm(0);
		}
	}

	if (optind == argc) {
		message("more arguments required");
		show_usage();
	}
}

static void parse_cmdline_search(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "explain", no_argument, NULL, 'e' },
		{ "format", required_argument, NULL, 'f' },
		{ "path", required_argument, NULL, 'p' },
		{ "location", no_argument, NULL, 'l' },
		{ "mode", required_argument, NULL, 'm' },
		{ "kind", required_argument, NULL, 'k' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL }
	};
	int c;

	while ((c = getopt_long(argc, argv, "+ef:m:k:p:lvh", long_options, NULL)) != -1) {
		switch (c) {
			case 'e':
				semind_search_by_location = EXPLAIN_LOCATION;
				break;
			case 'l':
				semind_search_by_location = USAGE_BY_LOCATION;
				break;
			case 'f':
				semind_search_format = optarg;
				break;
			case 'm':
				set_search_modmask(optarg);
				break;
			case 'k':
				semind_search_kind = tolower(optarg[0]);
				break;
			case 'p':
				semind_search_path = optarg;
				break;
			case 'v':
				semind_verbose++;
				break;
			case 'h':
				show_help_search(0);
		}
	}

	if (semind_search_by_location) {
		char *str;

		if (optind == argc)
			semind_error(1, 0, "one argument required");

		str = argv[optind];

		while (str) {
			char *ptr;

			if ((ptr = strchr(str, ':')) != NULL)
				*ptr++ = '\0';

			if (*str != '\0') {
				if (!semind_search_filename) {
					semind_search_filename = str;
				} else if (!semind_search_line) {
					semind_search_line = atoi(str);
				} else if (!semind_search_column) {
					semind_search_column = atoi(str);
				}
			}
			str = ptr;
		}
	} else if (optind < argc)
		semind_search_symbol = argv[optind++];
}

static int query_appendf(sqlite3_str *query, const char *fmt, ...)
{
	int status;
	va_list args;

	va_start(args, fmt);
	sqlite3_str_vappendf(query, fmt, args);
	va_end(args);

	if ((status = sqlite3_str_errcode(query)) == SQLITE_OK)
		return 0;

	if (status == SQLITE_NOMEM)
		message("not enough memory");

	if (status == SQLITE_TOOBIG)
		message("string too big");

	return -1;
}

static inline void sqlite_bind_text(sqlite3_stmt *stmt, const char *field, const char *var, int len)
{
	if (sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, field), var, len, SQLITE_STATIC) != SQLITE_OK)
		semind_error(1, 0, "unable to bind value for %s: %s", field, sqlite3_errmsg(semind_db));
}

static inline void sqlite_bind_int64(sqlite3_stmt *stmt, const char *field, long long var)
{
	if (sqlite3_bind_int64(stmt, sqlite3_bind_parameter_index(stmt, field), var) != SQLITE_OK)
		semind_error(1, 0, "unable to bind value for %s: %s", field, sqlite3_errmsg(semind_db));
}

static inline void sqlite_prepare(const char *sql, sqlite3_stmt **stmt)
{
	int ret;
	do {
		ret = sqlite3_prepare_v2(semind_db, sql, -1, stmt, NULL);
		if (ret != SQLITE_OK && ret != SQLITE_BUSY)
			semind_error(1, 0, "unable to prepare query: %s: %s", sqlite3_errmsg(semind_db), sql);
	} while (ret == SQLITE_BUSY);
}

static inline void sqlite_prepare_persistent(const char *sql, sqlite3_stmt **stmt)
{
	int ret;
	do {
		ret = sqlite3_prepare_v3(semind_db, sql, -1, SQLITE_PREPARE_PERSISTENT, stmt, NULL);
		if (ret != SQLITE_OK && ret != SQLITE_BUSY)
			semind_error(1, 0, "unable to prepare query: %s: %s", sqlite3_errmsg(semind_db), sql);
	} while (ret == SQLITE_BUSY);
}

static inline void sqlite_reset_stmt(sqlite3_stmt *stmt)
{
	// Contrary to the intuition of many, sqlite3_reset() does not reset the
	// bindings on a prepared statement. Use this routine to reset all host
	// parameters to NULL.
	sqlite3_clear_bindings(stmt);
	sqlite3_reset(stmt);
}

static int sqlite_run(sqlite3_stmt *stmt)
{
	int ret = sqlite3_step(stmt);
	if (ret != SQLITE_DONE && ret != SQLITE_ROW)
		semind_error(1, 0, "unable to process query: %s: %s", sqlite3_errmsg(semind_db), sqlite3_sql(stmt));
	return ret;
}

static void sqlite_command(const char *sql)
{
	sqlite3_stmt *stmt;
	sqlite_prepare(sql, &stmt);
	sqlite_run(stmt);
	sqlite3_finalize(stmt);
}

static sqlite3_int64 get_db_version(void)
{
	sqlite3_stmt *stmt;
	sqlite3_int64 dbversion;

	sqlite_prepare("PRAGMA user_version", &stmt);
	sqlite_run(stmt);
	dbversion = sqlite3_column_int64(stmt, 0);
	sqlite3_finalize(stmt);

	return dbversion;
}

static void set_db_version(void)
{
	char *sql;
	sqlite3_str *query = sqlite3_str_new(semind_db);

	if (query_appendf(query, "PRAGMA user_version = %d", SINDEX_DATABASE_VERSION) < 0)
		exit(1);

	sql = sqlite3_str_finish(query);
	sqlite_command(sql);
	sqlite3_free(sql);
}

static void open_temp_database(void)
{
	static const char *database_schema[] = {
		"ATTACH ':memory:' AS tempdb",
		"CREATE TABLE tempdb.semind ("
			" file INTEGER NOT NULL,"
			" line INTEGER NOT NULL,"
			" column INTEGER NOT NULL,"
			" symbol TEXT NOT NULL,"
			" kind INTEGER NOT NULL,"
			" context TEXT,"
			" mode INTEGER NOT NULL"
		")",
		NULL,
	};

	for (int i = 0; database_schema[i]; i++)
		sqlite_command(database_schema[i]);
}

static void open_database(const char *filename, int flags)
{
	static const char *database_schema[] = {
		"CREATE TABLE file ("
			" id INTEGER PRIMARY KEY AUTOINCREMENT,"
			" name TEXT UNIQUE NOT NULL,"
			" mtime INTEGER NOT NULL"
		")",
		"CREATE TABLE semind ("
			" file INTEGER NOT NULL REFERENCES file(id) ON DELETE CASCADE,"
			" line INTEGER NOT NULL,"
			" column INTEGER NOT NULL,"
			" symbol TEXT NOT NULL,"
			" kind INTEGER NOT NULL,"
			" context TEXT,"
			" mode INTEGER NOT NULL"
		")",
		"CREATE UNIQUE INDEX semind_0 ON semind (symbol, kind, mode, file, line, column)",
		"CREATE INDEX semind_1 ON semind (file)",
		NULL,
	};

	int exists = !access(filename, R_OK);

	if (sqlite3_open_v2(filename, &semind_db, flags, NULL) != SQLITE_OK)
		semind_error(1, 0, "unable to open database: %s: %s", filename, sqlite3_errmsg(semind_db));

	sqlite_command("PRAGMA journal_mode = WAL");
	sqlite_command("PRAGMA synchronous = OFF");
	sqlite_command("PRAGMA secure_delete = FAST");
	sqlite_command("PRAGMA busy_timeout = 2147483647");
	sqlite_command("PRAGMA foreign_keys = ON");

	if (exists) {
		if (get_db_version() < SINDEX_DATABASE_VERSION)
			semind_error(1, 0, "%s: Database too old. Please rebuild it.", filename);
		return;
	}

	set_db_version();

	for (int i = 0; database_schema[i]; i++)
		sqlite_command(database_schema[i]);
}

struct index_record {
	const char *context;
	int ctx_len;

	const char *symbol;
	int sym_len;

	int kind;
	unsigned int mode;
	long long mtime;
	sqlite3_int64 file;
	int line;
	int col;
};

static void insert_record(struct index_record *rec)
{
	sqlite_bind_text(insert_rec_stmt,  "@context", rec->context, rec->ctx_len);
	sqlite_bind_text(insert_rec_stmt,  "@symbol",  rec->symbol, rec->sym_len);
	sqlite_bind_int64(insert_rec_stmt, "@kind",    rec->kind);
	sqlite_bind_int64(insert_rec_stmt, "@mode",    rec->mode);
	sqlite_bind_int64(insert_rec_stmt, "@file",    rec->file);
	sqlite_bind_int64(insert_rec_stmt, "@line",    rec->line);
	sqlite_bind_int64(insert_rec_stmt, "@column",  rec->col);
	sqlite_run(insert_rec_stmt);
	sqlite_reset_stmt(insert_rec_stmt);
}

static void update_stream(void)
{
	if (semind_streams_nr >= input_stream_nr)
		return;

	semind_streams = realloc(semind_streams, input_stream_nr * sizeof(struct semind_streams));
	if (!semind_streams)
		semind_error(1, errno, "realloc");

	sqlite_run(lock_stmt);

	for (int i = semind_streams_nr; i < input_stream_nr; i++) {
		struct stat st;
		const char *filename;
		char fullname[PATH_MAX];
		sqlite3_int64 cur_mtime = 0;

		if (input_streams[i].fd != -1) {
			/*
			 * FIXME: Files in the input_streams may be duplicated.
			 */
			if (stat(input_streams[i].name, &st) < 0)
				semind_error(1, errno, "stat: %s", input_streams[i].name);

			cur_mtime = st.st_mtime;

			if (!realpath(input_streams[i].name, fullname))
				semind_error(1, errno, "realpath: %s", input_streams[i].name);

			if (!strncmp(fullname, cwd, n_cwd) && fullname[n_cwd] == '/') {
				filename = fullname + n_cwd + 1;
				semind_streams[i].id = 0;
			} else {
				semind_streams[i].id = -1;
				continue;
			}
		} else {
			semind_streams[i].id = -1;
			continue;
		}

		if (semind_verbose > 1)
			message("filename: %s", filename);

		sqlite_bind_text(select_file_stmt, "@name", filename, -1);

		if (sqlite_run(select_file_stmt) == SQLITE_ROW) {
			sqlite3_int64 old_mtime;

			semind_streams[i].id = sqlite3_column_int64(select_file_stmt, 0);
			old_mtime = sqlite3_column_int64(select_file_stmt, 1);

			sqlite_reset_stmt(select_file_stmt);

			if (cur_mtime == old_mtime)
				continue;

			sqlite_bind_text(delete_file_stmt, "@name", filename, -1);
			sqlite_run(delete_file_stmt);
			sqlite_reset_stmt(delete_file_stmt);
		}

		sqlite_reset_stmt(select_file_stmt);

		sqlite_bind_text(insert_file_stmt,  "@name",  filename, -1);
		sqlite_bind_int64(insert_file_stmt, "@mtime", cur_mtime);
		sqlite_run(insert_file_stmt);
		sqlite_reset_stmt(insert_file_stmt);

		semind_streams[i].id = sqlite3_last_insert_rowid(semind_db);
	}

	sqlite_run(unlock_stmt);

	semind_streams_nr = input_stream_nr;
}

static void r_symbol(unsigned mode, struct position *pos, struct symbol *sym)
{
	static struct ident null;
	struct ident *ctx = &null;
	struct index_record rec;

	update_stream();

	if (semind_streams[pos->stream].id == -1)
		return;

	if (!semind_include_local_syms && sym_is_local(sym))
		return;

	if (!sym->ident) {
		warning(*pos, "empty ident");
		return;
	}

	if (dissect_ctx)
		ctx = dissect_ctx->ident;

	rec.context = ctx->name;
	rec.ctx_len = ctx->len;
	rec.symbol  = sym->ident->name;
	rec.sym_len = sym->ident->len;
	rec.kind    = sym->kind;
	rec.mode    = mode;
	rec.file    = semind_streams[pos->stream].id;
	rec.line    = pos->line;
	rec.col     = pos->pos;

	insert_record(&rec);
}

static void r_member(unsigned mode, struct position *pos, struct symbol *sym, struct symbol *mem)
{
	static struct ident null;
	static char memname[1024];
	struct ident *ni, *si, *mi;
	struct ident *ctx = &null;
	struct index_record rec;

	update_stream();

	if (semind_streams[pos->stream].id == -1)
		return;

	if (!semind_include_local_syms && sym_is_local(sym))
		return;

	ni = built_in_ident("?");
	si = sym->ident ?: ni;
	/* mem == NULL means entire struct accessed */
	mi = mem ? (mem->ident ?: ni) : built_in_ident("*");

	if (dissect_ctx)
		ctx = dissect_ctx->ident;

	snprintf(memname, sizeof(memname), "%.*s.%.*s", si->len, si->name, mi->len, mi->name);

	rec.context = ctx->name;
	rec.ctx_len = ctx->len;
	rec.symbol  = memname;
	rec.sym_len = si->len + mi->len + 1;
	rec.kind    = 'm';
	rec.mode    = mode;
	rec.file    = semind_streams[pos->stream].id;
	rec.line    = pos->line;
	rec.col     = pos->pos;

	insert_record(&rec);
}

static void r_symdef(struct symbol *sym)
{
	r_symbol(U_DEF, &sym->pos, sym);
}

static void r_memdef(struct symbol *sym, struct symbol *mem)
{
	r_member(U_DEF, &mem->pos, sym, mem);
}

static void command_add(int argc, char **argv)
{
	static struct reporter reporter = {
		.r_symdef = r_symdef,
		.r_symbol = r_symbol,
		.r_memdef = r_memdef,
		.r_member = r_member,
	};

	open_temp_database();

	sqlite_prepare_persistent(
		"BEGIN IMMEDIATE",
		&lock_stmt);

	sqlite_prepare_persistent(
		"COMMIT",
		&unlock_stmt);

	sqlite_prepare_persistent(
		"INSERT OR IGNORE INTO tempdb.semind "
		"(context, symbol, kind, mode, file, line, column) "
		"VALUES (@context, @symbol, @kind, @mode, @file, @line, @column)",
		&insert_rec_stmt);

	sqlite_prepare_persistent(
		"SELECT id, mtime FROM file WHERE name == @name",
		&select_file_stmt);

	sqlite_prepare_persistent(
		"INSERT INTO file (name, mtime) VALUES (@name, @mtime)",
		&insert_file_stmt);

	sqlite_prepare_persistent(
		"DELETE FROM file WHERE name == @name",
		&delete_file_stmt);

	dissect(&reporter, semind_filelist);

	sqlite_run(lock_stmt);
	sqlite_command("INSERT OR IGNORE INTO semind SELECT * FROM tempdb.semind");
	sqlite_run(unlock_stmt);

	sqlite3_finalize(insert_rec_stmt);
	sqlite3_finalize(select_file_stmt);
	sqlite3_finalize(insert_file_stmt);
	sqlite3_finalize(delete_file_stmt);
	sqlite3_finalize(lock_stmt);
	sqlite3_finalize(unlock_stmt);
	free(semind_streams);
}

static void command_rm(int argc, char **argv)
{
	sqlite3_stmt *stmt;

	sqlite_command("BEGIN IMMEDIATE");
	sqlite_prepare("DELETE FROM file WHERE name GLOB @file", &stmt);

	if (semind_verbose > 1)
		message("SQL: %s", sqlite3_sql(stmt));

	for (int i = 0; i < argc; i++) {
		sqlite_bind_text(stmt, "@file",  argv[i], -1);
		sqlite_run(stmt);
		sqlite_reset_stmt(stmt);
	}

	sqlite3_finalize(stmt);
	sqlite_command("COMMIT");
}

static inline void print_mode(char *value)
{
	char str[3];
	int v = atoi(value);

	if (v == U_DEF) {
		printf("def");
		return;
	}

#define U(m) "-rwm"[(v / m) & 3]
	str[0] = U(U_R_AOF);
	str[1] = U(U_R_VAL);
	str[2] = U(U_R_PTR);

	printf("%.3s", str);
#undef U
}

static char *semind_file_name;
static FILE *semind_file_fd;
static int semind_file_lnum;
static char *semind_line;
static size_t semind_line_buflen;
static int semind_line_len;

static void print_file_line(const char *name, int lnum)
{
	/*
	 * All files are sorted by name and line number. So, we can reopen
	 * the file and read it line by line.
	 */
	if (!semind_file_name || strcmp(semind_file_name, name)) {
		if (semind_file_fd) {
			fclose(semind_file_fd);
			free(semind_file_name);
		}

		semind_file_name = strdup(name);

		if (!semind_file_name)
			semind_error(1, errno, "strdup");

		semind_file_fd = fopen(name, "r");

		if (!semind_file_fd)
			semind_error(1, errno, "fopen: %s", name);

		semind_file_lnum = 0;
	}

	do {
		if (semind_file_lnum == lnum) {
			if (semind_line[semind_line_len-1] == '\n')
				semind_line_len--;
			printf("%.*s", semind_line_len, semind_line);
			break;
		}
		semind_file_lnum++;
		errno = 0;
	} while((semind_line_len = getline(&semind_line, &semind_line_buflen, semind_file_fd)) != -1);

	if (errno && errno != EOF)
		semind_error(1, errno, "getline");
}

static int search_query_callback(void *data, int argc, char **argv, char **colname)
{
	char *fmt = (char *) semind_search_format;
	char buf[32];
	int quote = 0;
	int n = 0;

	while (*fmt != '\0') {
		char c = *fmt;

		if (quote) {
			quote = 0;
			switch (c) {
				case 't': c = '\t'; break;
				case 'r': c = '\r'; break;
				case 'n': c = '\n'; break;
			}
		} else if (c == '%') {
			int colnum = 0;
			char *pos = ++fmt;

			c = *fmt;

			if (c == '\0')
				semind_error(1, 0, "unexpected end of format string");

			switch (c) {
				case 'f': colnum = 0; goto print_string;
				case 'l': colnum = 1; goto print_string;
				case 'c': colnum = 2; goto print_string;
				case 'C': colnum = 3; goto print_string;
				case 'n': colnum = 4; goto print_string;
				case 'm':
					if (n) {
						printf("%.*s", n, buf);
						n = 0;
					}
					print_mode(argv[5]);
					fmt++;
					break;
				case 'k':
					if (n) {
						printf("%.*s", n, buf);
						n = 0;
					}
					printf("%c", atoi(argv[6]));
					fmt++;
					break;
				case 's':
					if (n) {
						printf("%.*s", n, buf);
						n = 0;
					}
					print_file_line(argv[0], atoi(argv[1]));
					fmt++;
					break;

				print_string:
					if (n) {
						printf("%.*s", n, buf);
						n = 0;
					}
					printf("%s", argv[colnum]);
					fmt++;
					break;
				default:
					break;

			}

			if (pos == fmt)
				semind_error(1, 0, "invalid format specification: %%%c", c);

			continue;
		} else if (c == '\\') {
			quote = 1;
			fmt++;
			continue;
		}

		if (n == sizeof(buf)) {
			printf("%.*s", n, buf);
			n = 0;
		}

		buf[n++] = c;
		fmt++;
	}

	if (n)
		printf("%.*s", n, buf);
	printf("\n");

	return 0;
}

static void command_search(int argc, char **argv)
{
	char *sql;
	char *dberr = NULL;
	sqlite3_str *query = sqlite3_str_new(semind_db);

	if (chdir(cwd) < 0)
		semind_error(1, errno, "unable to change directory: %s", cwd);

	if (query_appendf(query,
	                  "SELECT"
	                  " file.name,"
	                  " semind.line,"
	                  " semind.column,"
	                  " semind.context,"
	                  " semind.symbol,"
	                  " semind.mode,"
	                  " semind.kind "
	                  "FROM semind, file "
	                  "WHERE semind.file == file.id") < 0)
		goto fail;

	if (semind_search_kind) {
		if (query_appendf(query, " AND semind.kind == %d", semind_search_kind) < 0)
			goto fail;
	}

	if (semind_search_symbol) {
		int ret;

		if (query_appendf(query, " AND ") < 0)
			goto fail;

		if (strpbrk(semind_search_symbol, "*?[]"))
			ret = query_appendf(query, "semind.symbol GLOB %Q", semind_search_symbol);
		else
			ret = query_appendf(query, "semind.symbol == %Q", semind_search_symbol);

		if (ret < 0)
			goto fail;
	}

	if (semind_search_modmask_defined) {
		if (!semind_search_modmask) {
			if (query_appendf(query, " AND semind.mode == %d", semind_search_modmask) < 0)
				goto fail;
		} else if (query_appendf(query, " AND (semind.mode & %d) != 0", semind_search_modmask) < 0)
			goto fail;
	}

	if (semind_search_path) {
		if (query_appendf(query, " AND file.name GLOB %Q", semind_search_path) < 0)
			goto fail;
	}

	if (semind_search_by_location == EXPLAIN_LOCATION) {
		if (query_appendf(query, " AND file.name == %Q", semind_search_filename) < 0)
			goto fail;
		if (semind_search_line &&
		    query_appendf(query, " AND semind.line == %d", semind_search_line) < 0)
			goto fail;
		if (semind_search_column &&
		    query_appendf(query, " AND semind.column == %d", semind_search_column) < 0)
			goto fail;
	} else if (semind_search_by_location == USAGE_BY_LOCATION) {
		if (query_appendf(query, " AND semind.symbol IN (") < 0)
			goto fail;
		if (query_appendf(query,
		                 "SELECT semind.symbol FROM semind, file WHERE"
				 " semind.file == file.id AND"
		                 " file.name == %Q", semind_search_filename) < 0)
			goto fail;
		if (semind_search_line &&
		    query_appendf(query, " AND semind.line == %d", semind_search_line) < 0)
			goto fail;
		if (semind_search_column &&
		    query_appendf(query, " AND semind.column == %d", semind_search_column) < 0)
			goto fail;
		if (query_appendf(query, ")") < 0)
			goto fail;
	}

	if (query_appendf(query, " ORDER BY file.name, semind.line, semind.column ASC", semind_search_path) < 0)
		goto fail;

	sql = sqlite3_str_value(query);

	if (semind_verbose > 1)
		message("SQL: %s", sql);

	sqlite3_exec(semind_db, sql, search_query_callback, NULL, &dberr);
	if (dberr)
		semind_error(1, 0, "sql query failed: %s", dberr);
fail:
	sql = sqlite3_str_finish(query);
	sqlite3_free(sql);

	if (semind_file_fd) {
		fclose(semind_file_fd);
		free(semind_file_name);
	}
	free(semind_line);
}


int main(int argc, char **argv)
{
	static const struct command commands[] = {
		{
			.name          = "add",
			.dbflags       = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
			.parse_cmdline = parse_cmdline_add,
			.handler       = command_add
		},
		{
			.name          = "rm",
			.dbflags       = SQLITE_OPEN_READWRITE,
			.parse_cmdline = parse_cmdline_rm,
			.handler       = command_rm
		},
		{
			.name          = "search",
			.dbflags       = SQLITE_OPEN_READONLY,
			.parse_cmdline = parse_cmdline_search,
			.handler       = command_search
		},
		{ .name = NULL },
	};
	const struct command *cmd;

	if (!(progname = rindex(argv[0], '/')))
		progname = argv[0];
	else
		progname++;

	if (!realpath(".", cwd))
		semind_error(1, errno, "unable to get current directory");
	n_cwd = strlen(cwd);

	parse_cmdline(argc, argv);

	for (cmd = commands; cmd->name && strcmp(argv[optind], cmd->name); cmd++);
	if (!cmd->name)
		semind_error(1, 0, "unknown command: %s", argv[optind]);
	optind++;

	semind_command = cmd->name;

	if (cmd->parse_cmdline)
		cmd->parse_cmdline(argc, argv);

	open_database(semind_dbfile, cmd->dbflags);
	cmd->handler(argc - optind, argv + optind);

	sqlite3_close(semind_db);

	return 0;
}
