/*
 * Sparse identifier tagger
 *
 * Copyright (C) 2026 Dan Carpenter
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <ctype.h>
#include <db.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include "dissect.h"
#include "options.h"
#include "scope.h"

enum destination_type {
	BASE,
	NORMAL,
	LOOKUP,
	BASE_FUNCTION,
	BASE_ARGUMENT,
	BASE_MEMBER,
};

struct tag {
	unsigned int file;
	unsigned int dest;
	unsigned int line;
	unsigned int pos;
	unsigned int dest_line;
	unsigned int dest_pos;
	enum destination_type type;
	struct tag *next;
};

struct global_definition {
	char *name;
	unsigned int file;
	unsigned int line;
	unsigned int pos;
	struct global_definition *next;
	struct global_definition *hash_next;
};

struct source_reader {
	const char *filename;
	FILE *file;
	char *line;
	size_t capacity;
	unsigned int line_number;
	struct source_reader *next;
};

struct compound_definition {
	struct ident *ident;
	struct position pos;
	struct compound_definition *next;
};

#define TAG_BATCH_SIZE 4096
#define TAG_MAX_RETRIES 1000
#define GLOBAL_HASH_SIZE 1024
#define MAX_FILE_NUMBER 0xffffff
#define MAX_LINE_NUMBER 0xffffff
#define MAX_POSITION 0x3ff

static struct string_list *source_files;
static struct symbol_list *called_functions;
static struct tag *tags;
static struct tag **next_tag = &tags;
static struct global_definition *global_definitions;
static struct global_definition **next_global_definition =
	&global_definitions;
static struct global_definition *global_hash[GLOBAL_HASH_SIZE];
static struct source_reader *source_readers;
static struct compound_definition *compound_definitions;

static DB *file_numbers;
static DB_ENV *file_env;

static int symbol_is_function(struct symbol *sym);
static struct position owner_position(struct symbol *sym);

static int base_type(enum destination_type type)
{
	return type == BASE || type == BASE_FUNCTION ||
	       type == BASE_ARGUMENT || type == BASE_MEMBER;
}

static unsigned int global_hash_bucket(const char *name)
{
	unsigned long hash = 5381;

	while (*name)
		hash = ((hash << 5) + hash) + (unsigned char)*name++;
	return hash % GLOBAL_HASH_SIZE;
}

static int open_file_numbers(const char *db_dir)
{
	int flags;
	int ret;

	flags = DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_INIT_TXN |
		DB_THREAD;
	ret = db_env_create(&file_env, 0);
	if (ret)
		return ret;
	ret = file_env->open(file_env, db_dir, flags, 0);
	if (ret)
		return ret;
	ret = db_create(&file_numbers, file_env, 0);
	if (ret)
		return ret;
	return file_numbers->open(file_numbers, NULL, "file_numbers.db", NULL,
				  DB_BTREE, DB_THREAD | DB_RDONLY, 0);
}

static void close_file_numbers(void)
{
	if (file_numbers)
		file_numbers->close(file_numbers, 0);
	if (file_env)
		file_env->close(file_env, 0);
	file_numbers = NULL;
	file_env = NULL;
}

static int get_file_number(const char *filename, unsigned int *number)
{
	unsigned char value_buf[3];
	DBT key = { 0 };
	DBT value = { 0 };
	int ret;

	key.data = (void *)filename;
	key.size = strlen(filename);
	value.data = value_buf;
	value.ulen = sizeof(value_buf);
	value.flags = DB_DBT_USERMEM;
	ret = file_numbers->get(file_numbers, NULL, &key, &value, 0);
	if (ret)
		return ret;
	if (value.size != sizeof(value_buf))
		return EINVAL;
	*number = ((unsigned int)value_buf[0] << 16) |
		  ((unsigned int)value_buf[1] << 8) | value_buf[2];
	return 0;
}

static void save_tag(struct position *pos, const char *name,
		     enum destination_type type, unsigned int dest,
		     unsigned int dest_line, unsigned int dest_pos)
{
	struct tag *tag;
	unsigned int file;

	(void)name;
	if (get_file_number(stream_name(pos->stream), &file))
		return;
	if (file > MAX_FILE_NUMBER || pos->line > MAX_LINE_NUMBER ||
	    pos->pos > MAX_POSITION || dest > MAX_FILE_NUMBER ||
	    dest_line > MAX_LINE_NUMBER || dest_pos > MAX_POSITION ||
	    type > 0x3f)
		return;

	tag = calloc(1, sizeof(*tag));
	if (!tag)
		die("out of memory\n");
	tag->file = file;
	tag->line = pos->line;
	tag->pos = pos->pos;
	tag->type = type;
	tag->dest = dest;
	tag->dest_line = dest_line;
	tag->dest_pos = dest_pos;
	*next_tag = tag;
	next_tag = &tag->next;
}

static void save_global_implementation(struct symbol *sym,
				       struct position *pos)
{
	struct global_definition *definition;
	struct ident *ident;
	const char *name;
	unsigned int file;
	unsigned int bucket;

	ident = sym->ident;
	if (!ident || ident->reserved)
		return;
	name = show_ident(ident);
	bucket = global_hash_bucket(name);
	for (definition = global_hash[bucket]; definition;
	     definition = definition->hash_next) {
		if (!strcmp(definition->name, name))
			return;
	}
	if (get_file_number(stream_name(pos->stream), &file))
		return;
	if (file > MAX_FILE_NUMBER || pos->line > MAX_LINE_NUMBER ||
	    pos->pos > MAX_POSITION)
		return;

	definition = calloc(1, sizeof(*definition));
	if (!definition)
		die("out of memory\n");
	definition->name = strdup(name);
	if (!definition->name)
		die("out of memory\n");
	definition->file = file;
	definition->line = pos->line;
	definition->pos = pos->pos;
	definition->hash_next = global_hash[bucket];
	global_hash[bucket] = definition;
	*next_global_definition = definition;
	next_global_definition = &definition->next;
}

static void save_global_symbol(struct symbol *sym, struct position *pos)
{
	struct symbol *type;

	if (!sym || sym->scope != global_scope)
		return;
	type = sym->ctype.base_type;
	if ((sym->ctype.modifiers & MOD_EXTERN) &&
	    (!type || type->type != SYM_FN))
		return;
	if (type && type->type == SYM_FN &&
	    !type->stmt && !type->inline_stmt)
		return;
	save_global_implementation(sym, pos);
}

static int header_file(struct position *pos)
{
	const char *file = stream_name(pos->stream);
	size_t len = strlen(file);

	return len >= 2 && !strcmp(file + len - 2, ".h");
}

static void save_global_struct(struct symbol *sym, struct position *pos)
{
	if (!sym || sym->type != SYM_STRUCT || !sym->ident ||
	    !sym->symbol_list || !header_file(pos))
		return;
	save_global_implementation(sym, pos);
}

static int function_was_called(struct symbol *sym)
{
	struct symbol *tmp;

	FOR_EACH_PTR(called_functions, tmp) {
		if (tmp == sym)
			return 1;
	} END_FOR_EACH_PTR(tmp);

	return 0;
}

static int source_position(struct position *pos)
{
	const char *file;
	char *source;

	file = stream_name(pos->stream);
	FOR_EACH_PTR(source_files, source) {
		if (!strcmp(file, source))
			return 1;
	} END_FOR_EACH_PTR(source);
	if (dissect_ctx && function_was_called(dissect_ctx))
		return 1;

	return 0;
}

static int same_position(struct position *one, struct position *two)
{
	return one->stream == two->stream && one->line == two->line &&
	       one->pos == two->pos;
}

static struct source_reader *get_source_reader(const char *filename)
{
	struct source_reader *reader;

	for (reader = source_readers; reader; reader = reader->next) {
		if (!strcmp(reader->filename, filename))
			return reader;
	}
	reader = calloc(1, sizeof(*reader));
	if (!reader)
		die("out of memory\n");
	reader->filename = filename;
	reader->file = fopen(filename, "r");
	if (!reader->file) {
		free(reader);
		return NULL;
	}
	reader->next = source_readers;
	source_readers = reader;
	return reader;
}

static char *get_source_line(struct position *pos)
{
	struct source_reader *reader;

	reader = get_source_reader(stream_name(pos->stream));
	if (!reader)
		return NULL;
	if (pos->line < reader->line_number) {
		rewind(reader->file);
		reader->line_number = 0;
	}
	while (reader->line_number < pos->line) {
		if (getline(&reader->line, &reader->capacity, reader->file) < 0)
			return NULL;
		reader->line_number++;
	}
	return reader->line;
}

static int identifier_char(char c)
{
	return isalnum((unsigned char)c) || c == '_';
}

static struct position identifier_position(struct position *pos,
					   struct ident *ident)
{
	struct position result = *pos;
	const char *name;
	unsigned int column = 1;
	size_t len;
	char *line;
	char *p;

	if (!ident)
		return result;
	name = show_ident(ident);
	len = strlen(name);
	line = get_source_line(pos);
	if (!line)
		return result;
	for (p = line; *p; p++) {
		if (column >= pos->pos && !strncmp(p, name, len) &&
		    (p == line || !identifier_char(p[-1])) &&
		    !identifier_char(p[len])) {
			result.pos = column;
			return result;
		}
		if (*p == '\t')
			column += 8 - ((column - 1) % 8);
		else
			column++;
	}
	return result;
}

static int identifier_at_position(struct position *pos, struct ident *ident)
{
	const char *name;
	unsigned int column = 1;
	size_t len;
	char *line;
	char *p;

	if (!ident)
		return 0;
	name = show_ident(ident);
	len = strlen(name);
	line = get_source_line(pos);
	if (!line)
		return 0;
	for (p = line; *p && column < pos->pos; p++) {
		if (*p == '\t')
			column += 8 - ((column - 1) % 8);
		else
			column++;
	}
	return column == pos->pos && !strncmp(p, name, len) &&
	       (p == line || !identifier_char(p[-1])) &&
	       !identifier_char(p[len]);
}

static void remember_compound_definition(struct symbol *sym,
					 struct position *pos)
{
	struct compound_definition *definition;

	if (!sym->ident ||
	    (sym->type != SYM_STRUCT && sym->type != SYM_UNION) ||
	    !identifier_at_position(pos, sym->ident))
		return;
	for (definition = compound_definitions; definition;
	     definition = definition->next) {
		if (definition->ident == sym->ident) {
			definition->pos = *pos;
			return;
		}
	}
	definition = malloc(sizeof(*definition));
	if (!definition)
		die("out of memory\n");
	definition->ident = sym->ident;
	definition->pos = *pos;
	definition->next = compound_definitions;
	compound_definitions = definition;
}

static int enclosing_compound_position(struct symbol *sym,
				       struct position *pos)
{
	struct compound_definition *definition;
	const char *colon;
	const char *name;
	size_t parent_len = 0;

	*pos = owner_position(sym);
	if (pos->pos == MAX_POSITION ||
	    identifier_at_position(pos, sym->ident))
		return 1;
	name = show_ident(sym->ident);
	colon = strchr(name, ':');
	if (colon)
		parent_len = colon - name;
	for (definition = compound_definitions; definition;
	     definition = definition->next) {
		if (definition->ident == sym->ident ||
		    (parent_len &&
		     strlen(show_ident(definition->ident)) == parent_len &&
		     !strncmp(show_ident(definition->ident), name, parent_len))) {
			*pos = definition->pos;
			return 1;
		}
	}
	return 0;
}

static int identifier_before(struct position *pos, struct ident *ident,
			     unsigned int before, struct position *result)
{
	const char *name;
	unsigned int column = 1;
	unsigned int found = 0;
	size_t len;
	char *line;
	char *p;

	if (!ident)
		return 0;
	name = show_ident(ident);
	len = strlen(name);
	line = get_source_line(pos);
	if (!line)
		return 0;
	for (p = line; *p && column < before; p++) {
		if (!strncmp(p, name, len) &&
		    (p == line || !identifier_char(p[-1])) &&
		    !identifier_char(p[len]))
			found = column;
		if (*p == '\t')
			column += 8 - ((column - 1) % 8);
		else
			column++;
	}
	if (!found)
		return 0;
	*result = *pos;
	result->pos = found;
	return 1;
}

static struct symbol *get_named_type(struct symbol *sym)
{
	struct symbol *type;
	int depth;

	type = sym ? sym->ctype.base_type : NULL;
	for (depth = 0; type && depth < 16; depth++) {
		if ((type->type == SYM_STRUCT || type->type == SYM_UNION ||
		     type->type == SYM_ENUM) && type->ident)
			return type;
		type = type->ctype.base_type;
	}
	return NULL;
}

static void save_definition_type(struct symbol *sym,
				 struct position *identifier_pos)
{
	struct position source_pos;
	struct position dest_pos;
	struct symbol *type;
	const char *name;
	unsigned int dest;

	type = get_named_type(sym);
	if (!type || !identifier_before(&sym->pos, type->ident,
					identifier_pos->pos, &source_pos))
		return;
	dest_pos = identifier_position(&type->pos, type->ident);
	name = show_ident(type->ident);
	if (same_position(&source_pos, &dest_pos)) {
		save_tag(&source_pos, name, BASE, 0, 0, 0);
		return;
	}
	if (get_file_number(stream_name(dest_pos.stream), &dest))
		return;
	save_tag(&source_pos, name, NORMAL, dest, dest_pos.line, dest_pos.pos);
}

static void save_function_argument_types(struct symbol *sym)
{
	struct symbol *argument;
	struct symbol *type;
	struct position pos;

	if (!sym)
		return;
	type = sym->ctype.base_type;
	if (!type || type->type != SYM_FN)
		return;
	FOR_EACH_PTR(type->arguments, argument) {
		if (!argument->ident)
			continue;
		pos = identifier_position(&argument->pos, argument->ident);
		save_definition_type(argument, &pos);
	} END_FOR_EACH_PTR(argument);
}

static int member_operator_width(struct position *pos)
{
	unsigned int column = 1;
	char *line;
	char *p;

	line = get_source_line(pos);
	if (!line)
		return 0;
	for (p = line; *p && column < pos->pos; p++) {
		if (*p == '\t')
			column += 8 - ((column - 1) % 8);
		else
			column++;
	}
	if (column != pos->pos)
		return 0;
	if (*p == '-' && p[1] == '>')
		return 2;
	if (*p == '.')
		return 1;
	return 0;
}

static int show_macro(struct position *pos)
{
	return !!get_macro_name(*pos);
}

static void record_macro_uses(void)
{
	struct macro_expansion *expansion;
	unsigned int dest;

	for (expansion = get_macro_expansions(); expansion;
	     expansion = expansion->next) {
		if (!source_position(&expansion->pos))
			continue;
		if (get_file_number(stream_name(expansion->definition.stream),
				    &dest))
			continue;
		save_tag(&expansion->pos, expansion->name, NORMAL, dest,
			 expansion->definition.line, expansion->definition.pos);
	}
}

static struct symbol *get_implementation(struct symbol *sym)
{
	struct symbol *type;

	type = sym->ctype.base_type;
	if (!type || type->type != SYM_FN)
		return sym;
	return sym->definition;
}

static int symbol_is_function(struct symbol *sym)
{
	struct symbol *type;

	type = sym->ctype.base_type;
	return type && type->type == SYM_FN;
}

static void save_function_position(struct symbol *sym, struct position *pos)
{
	struct symbol *implementation;
	struct symbol *type;
	struct position implementation_pos;
	unsigned int file;

	implementation = get_implementation(sym);
	type = sym->ctype.base_type;
	if (!implementation && type &&
	    (type->stmt || type->inline_stmt))
		implementation = sym;
	if (!implementation) {
		save_tag(pos, show_ident(sym->ident), LOOKUP, 0, 0, 0);
		return;
	}
	implementation_pos = owner_position(implementation);
	if (get_file_number(stream_name(implementation_pos.stream), &file))
		return;
	save_tag(pos, show_ident(sym->ident), NORMAL, file,
		 implementation_pos.line, implementation_pos.pos);
}

static int argument_number(struct symbol *function, struct symbol *argument)
{
	struct symbol *type;
	struct symbol *sym;
	int number = 0;

	if (!function)
		return -1;
	type = function->ctype.base_type;
	if (!type || type->type != SYM_FN)
		return -1;
	FOR_EACH_PTR(type->arguments, sym) {
		if (sym == argument)
			return number;
		number++;
	} END_FOR_EACH_PTR(sym);
	return -1;
}

static struct position owner_position(struct symbol *sym)
{
	struct position pos;

	pos = identifier_position(&sym->pos, sym->ident);
	if (in_macro(sym->pos))
		pos.pos = MAX_POSITION;
	return pos;
}

static void record_function_call(unsigned mode, struct symbol *sym)
{
	struct symbol *implementation;

	if (!(mode & U_R_PTR) || !sym || !symbol_is_function(sym))
		return;
	implementation = get_implementation(sym);
	if (!implementation || function_was_called(implementation))
		return;
	add_symbol(&called_functions, implementation);
}

static bool follow_function(struct symbol *sym)
{
	const char *file;
	char *source;

	if (function_was_called(sym))
		return true;
	file = stream_name(sym->pos.stream);
	FOR_EACH_PTR(source_files, source) {
		if (!strcmp(file, source))
			return true;
	} END_FOR_EACH_PTR(source);

	return false;
}

static void show_identifier(struct position *pos, struct symbol *sym,
			    int definition)
{
	struct symbol *implementation;
	struct position implementation_pos;
	struct ident *ident;
	unsigned int dest;

	if (!sym)
		return;
	if (!source_position(pos) && !function_was_called(sym))
		return;
	if (show_macro(pos))
		return;
	ident = sym->ident;
	if (!ident || ident->reserved)
		return;
	if (definition) {
		save_tag(pos, show_ident(ident), BASE, 0, 0, 0);
		return;
	}
	implementation = get_implementation(sym);
	if (sym->ctype.modifiers & MOD_EXTERN) {
		save_tag(pos, show_ident(ident), LOOKUP, 0, 0, 0);
		return;
	}
	if (!implementation)
		return;
	implementation_pos = identifier_position(
		&implementation->pos, implementation->ident);
	if (same_position(pos, &implementation_pos)) {
		save_tag(pos, show_ident(ident), BASE, 0, 0, 0);
		return;
	}

	if (get_file_number(stream_name(implementation_pos.stream), &dest))
		return;
	save_tag(pos, show_ident(ident), NORMAL, dest,
		 implementation_pos.line, implementation_pos.pos);
}

static void encode_position(unsigned char *buf, unsigned int file,
			    unsigned int line, unsigned int pos,
			    enum destination_type type)
{
	unsigned long long packed;
	int i;

	packed = ((unsigned long long)file << 40) |
		 ((unsigned long long)line << 16) |
		 ((unsigned long long)pos << 6) | type;
	for (i = 0; i < 8; i++)
		buf[i] = packed >> (56 - i * 8);
}

static int put_tag(DB_TXN *txn, DB *source, DB *destination, struct tag *tag)
{
	unsigned char source_key_buf[8];
	unsigned char dest_key_buf[8];
	unsigned char source_value_buf[8];
	unsigned char dest_value_buf[8];
	DBT source_key = { 0 };
	DBT source_value = { 0 };
	DBT dest_key = { 0 };
	DBT dest_value = { 0 };
	int ret;

	encode_position(source_key_buf, tag->file, tag->line, tag->pos, 0);
	encode_position(dest_key_buf, tag->dest, tag->dest_line,
			tag->dest_pos, 0);
	encode_position(source_value_buf, tag->dest, tag->dest_line,
			tag->dest_pos, tag->type);
	encode_position(dest_value_buf, tag->file, tag->line, tag->pos,
			tag->type);
	source_key.data = source_key_buf;
	source_key.size = sizeof(source_key_buf);
	source_value.data = source_value_buf;
	source_value.size = sizeof(source_value_buf);
	dest_key.data = dest_key_buf;
	dest_key.size = sizeof(dest_key_buf);
	dest_value.data = dest_value_buf;
	dest_value.size = sizeof(dest_value_buf);

	ret = source->put(source, txn, &source_key, &source_value, 0);
	if (ret)
		return ret;
	if (base_type(tag->type))
		return 0;
	ret = destination->put(destination, txn, &dest_key, &dest_value,
			       DB_NODUPDATA);
	if (ret == DB_KEYEXIST)
		return 0;
	return ret;
}

static int put_global_definition(DB_TXN *txn, DB *globals,
				 struct global_definition *definition)
{
	unsigned char value_buf[8];
	DBT key = { 0 };
	DBT value = { 0 };
	int ret;

	encode_position(value_buf, definition->file, definition->line,
			definition->pos, LOOKUP);
	key.data = (void *)definition->name;
	key.size = strlen(definition->name);
	value.data = value_buf;
	value.size = sizeof(value_buf);
	ret = globals->put(globals, txn, &key, &value, DB_NOOVERWRITE);
	if (ret == DB_KEYEXIST)
		return 0;
	return ret;
}

static int mark_parsed_files(DB_ENV *env, DB *parsed)
{
	unsigned char key_buf[3];
	static const char value_buf = 1;
	char *file;
	unsigned int number;
	DB_TXN *txn;
	DBT key = { 0 };
	DBT value = { 0 };
	int ret;

	ret = env->txn_begin(env, NULL, &txn, 0);
	if (ret)
		return ret;
	FOR_EACH_PTR(source_files, file) {
		if (get_file_number(file, &number))
			continue;
		key_buf[0] = number >> 16;
		key_buf[1] = number >> 8;
		key_buf[2] = number;
		key.data = key_buf;
		key.size = sizeof(key_buf);
		value.data = (void *)&value_buf;
		value.size = sizeof(value_buf);
		ret = parsed->put(parsed, txn, &key, &value, 0);
		if (ret)
			break;
	} END_FOR_EACH_PTR(file);
	if (ret)
		txn->abort(txn);
	else
		ret = txn->commit(txn, DB_TXN_NOSYNC);
	return ret;
}

static int write_batch(DB_ENV *env, DB *source, DB *destination,
		       struct tag *first, struct tag *end)
{
	DB_TXN *txn;
	struct tag *tag;
	int retries = 0;
	int ret;

retry:
	ret = env->txn_begin(env, NULL, &txn, DB_TXN_NOWAIT);
	if (ret)
		return ret;
	for (tag = first; tag != end; tag = tag->next) {
		ret = put_tag(txn, source, destination, tag);
		if (ret)
			break;
	}
	if (!ret)
		ret = txn->commit(txn, DB_TXN_NOSYNC);
	else
		txn->abort(txn);
	if ((ret == DB_LOCK_DEADLOCK || ret == DB_LOCK_NOTGRANTED) &&
	    retries++ < TAG_MAX_RETRIES) {
		usleep(1000);
		goto retry;
	}
	return ret;
}

static int write_global_batch(DB_ENV *env, DB *globals,
			      struct global_definition *first,
			      struct global_definition *end)
{
	struct global_definition *definition;
	DB_TXN *txn;
	int retries = 0;
	int ret;

retry:
	ret = env->txn_begin(env, NULL, &txn, DB_TXN_NOWAIT);
	if (ret)
		return ret;
	for (definition = first; definition != end;
	     definition = definition->next) {
		ret = put_global_definition(txn, globals, definition);
		if (ret)
			break;
	}
	if (!ret)
		ret = txn->commit(txn, DB_TXN_NOSYNC);
	else
		txn->abort(txn);
	if ((ret == DB_LOCK_DEADLOCK || ret == DB_LOCK_NOTGRANTED) &&
	    retries++ < TAG_MAX_RETRIES) {
		usleep(1000);
		goto retry;
	}
	return ret;
}

static int write_tags(const char *db_dir)
{
	DB_ENV *env = NULL;
	DB *source = NULL;
	DB *destination = NULL;
	DB *globals = NULL;
	DB *parsed = NULL;
	struct global_definition *first_global;
	struct global_definition *end_global;
	struct tag *first;
	struct tag *end;
	int count;
	int lock_fd = -1;
	int flags;
	int ret;
	char lock_path[PATH_MAX];

	if (snprintf(lock_path, sizeof(lock_path), "%s/writer.lock", db_dir) >=
	    (int)sizeof(lock_path)) {
		ret = ENAMETOOLONG;
		goto out;
	}
	lock_fd = open(lock_path, O_CREAT | O_RDWR, 0666);
	if (lock_fd < 0) {
		ret = errno;
		goto out;
	}
	if (flock(lock_fd, LOCK_EX)) {
		ret = errno;
		goto out;
	}

	flags = DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_INIT_TXN |
		DB_THREAD;
	ret = db_env_create(&env, 0);
	if (ret)
		goto out;
	env->set_lk_detect(env, DB_LOCK_DEFAULT);
	ret = env->open(env, db_dir, flags, 0);
	if (ret)
		goto out;
	ret = db_create(&source, env, 0);
	if (ret)
		goto out;
	ret = source->open(source, NULL, "source.db", NULL, DB_BTREE,
			   DB_AUTO_COMMIT | DB_THREAD, 0);
	if (ret)
		goto out;
	ret = db_create(&destination, env, 0);
	if (ret)
		goto out;
	ret = destination->open(destination, NULL, "destination.db", NULL,
				DB_BTREE, DB_AUTO_COMMIT | DB_THREAD, 0);
	if (ret)
		goto out;
	ret = db_create(&globals, env, 0);
	if (ret)
		goto out;
	ret = globals->open(globals, NULL, "globals.db", NULL, DB_BTREE,
			    DB_AUTO_COMMIT | DB_THREAD, 0);
	if (ret)
		goto out;
	ret = db_create(&parsed, env, 0);
	if (ret)
		goto out;
	ret = parsed->open(parsed, NULL, "parsed_files.db", NULL, DB_BTREE,
			  DB_AUTO_COMMIT | DB_THREAD, 0);
	if (ret)
		goto out;

	first = tags;
	while (first) {
		end = first;
		for (count = 0; end && count < TAG_BATCH_SIZE; count++)
			end = end->next;
		ret = write_batch(env, source, destination, first, end);
		if (ret)
			goto out;
		first = end;
	}
	first_global = global_definitions;
	while (first_global) {
		end_global = first_global;
		for (count = 0; end_global && count < TAG_BATCH_SIZE; count++)
			end_global = end_global->next;
		ret = write_global_batch(env, globals, first_global, end_global);
		if (ret)
			goto out;
		first_global = end_global;
	}
	ret = mark_parsed_files(env, parsed);
out:
	if (parsed)
		parsed->close(parsed, 0);
	if (globals)
		globals->close(globals, 0);
	if (destination)
		destination->close(destination, 0);
	if (source)
		source->close(source, 0);
	if (env)
		env->close(env, 0);
	if (lock_fd >= 0) {
		flock(lock_fd, LOCK_UN);
		close(lock_fd);
	}
	if (ret)
		fprintf(stderr, "tagger: %s: %s\n", db_dir, db_strerror(ret));
	return ret;
}

static void report_symbol_definition(struct symbol *sym)
{
	struct position function_pos;
	struct symbol *member;
	struct position pos;
	int argument;

	pos = identifier_position(&sym->pos, sym->ident);
	remember_compound_definition(sym, &pos);
	save_global_symbol(sym, &pos);
	save_global_struct(sym, &pos);
	save_definition_type(sym, &pos);
	save_function_argument_types(sym);
	if ((sym->type == SYM_STRUCT || sym->type == SYM_UNION) &&
	    sym->ident && !sym->symbol_list) {
		save_tag(&pos, show_ident(sym->ident), LOOKUP, 0, 0, 0);
		return;
	}
	if (sym->type == SYM_STRUCT && sym->ident && sym->symbol_list &&
	    header_file(&pos)) {
		save_tag(&pos, show_ident(sym->ident), BASE, 0, 0, 0);
		return;
	}
	if (sym->enum_member && sym->ident) {
		save_tag(&pos, show_ident(sym->ident), BASE, 0, 0, 0);
		return;
	}
	if (sym->namespace == NS_TYPEDEF && sym->ident) {
		save_tag(&pos, show_ident(sym->ident), BASE, 0, 0, 0);
		return;
	}
	if (sym->type == SYM_ENUM) {
		if (sym->ident)
			save_tag(&pos, show_ident(sym->ident), BASE, 0, 0, 0);
		FOR_EACH_PTR(sym->symbol_list, member) {
			if (!member->ident)
				continue;
			pos = identifier_position(&member->pos, member->ident);
			save_tag(&pos, show_ident(member->ident), BASE, 0, 0, 0);
		} END_FOR_EACH_PTR(member);
		return;
	}
	argument = argument_number(dissect_ctx, sym);
	if (argument >= 0 && sym->ident) {
		function_pos = owner_position(dissect_ctx);
		save_tag(&pos, show_ident(sym->ident), BASE_ARGUMENT,
			 argument, function_pos.line, function_pos.pos);
		return;
	}
	if (symbol_is_function(sym) && sym->ident) {
		save_function_position(sym, &pos);
		return;
	}
	show_identifier(&pos, sym, 1);
}

static void report_member_definition(struct symbol *sym, struct symbol *member)
{
	struct position owner_pos;
	struct position pos;
	unsigned int file;

	pos = identifier_position(&member->pos, member->ident);
	if (sym && sym->ident && member->ident &&
	    (sym->type == SYM_STRUCT || sym->type == SYM_UNION)) {
		if (enclosing_compound_position(sym, &owner_pos) &&
		    !get_file_number(stream_name(owner_pos.stream), &file)) {
			save_tag(&pos, show_ident(member->ident), BASE_MEMBER,
				 file, owner_pos.line, owner_pos.pos);
			return;
		}
	}
	show_identifier(&pos, member, 1);
}

static void report_symbol(unsigned mode, struct position *pos,
			  struct symbol *sym)
{
	record_function_call(mode, sym);
	show_identifier(pos, sym, 0);
}

static void report_member(unsigned mode, struct position *pos,
			  struct symbol *sym, struct symbol *member)
{
	struct position member_pos = *pos;
	int width;

	width = member_operator_width(pos);
	if (!width)
		return;
	member_pos.pos += width;
	show_identifier(&member_pos, member, 0);
}

static void report_label(struct position *pos, struct symbol *label,
			 int definition)
{
	struct position dest_pos;
	unsigned int file;

	if (!label || !label->ident || !source_position(pos))
		return;
	if (definition) {
		save_tag(pos, show_ident(label->ident), BASE, 0, 0, 0);
		return;
	}
	if (!label->stmt)
		return;
	dest_pos = label->stmt->pos;
	if (get_file_number(stream_name(dest_pos.stream), &file))
		return;
	save_tag(pos, show_ident(label->ident), NORMAL, file,
		 dest_pos.line, dest_pos.pos);
}

int main(int argc, char **argv)
{
	static struct reporter reporter = {
		.r_follow = follow_function,
		.r_memdef = report_member_definition,
		.r_member = report_member,
		.r_symdef = report_symbol_definition,
		.r_symbol = report_symbol,
		.r_label = report_label,
	};
	struct string_list *filelist = NULL;
	const char *db_dir;
	int i;

	if (argc < 2)
		die("usage: tagger <database directory> [sparse options] file.c\n");
	db_dir = argv[1];
	if (open_file_numbers(db_dir))
		die("tagger: cannot open file number database\n");
	for (i = 1; i < argc - 1; i++)
		argv[i] = argv[i + 1];
	argc--;
	argv[argc] = NULL;

	sparse_initialize(argc, argv, &filelist);
	source_files = filelist;
	dissect_show_all_symbols = 1;
	dissect(&reporter, filelist);
	record_macro_uses();

	i = write_tags(db_dir) != 0;
	close_file_numbers();
	return i;
}
