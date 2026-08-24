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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <db.h>

#include "dissect.h"
#include "options.h"

enum destination_type {
	BASE,
	NORMAL,
	LOOKUP,
};

struct macro_use {
	struct position pos;
	const char *name;
	struct macro_use *next;
};

struct tag {
	unsigned long long file;
	unsigned long long dest;
	unsigned int line;
	unsigned int pos;
	unsigned int dest_line;
	unsigned int dest_pos;
	enum destination_type type;
	char *identifier;
	struct tag *next;
};

#define TAG_BATCH_SIZE 1000

static struct string_list *source_files;
static struct symbol_list *called_functions;
static struct macro_use *macro_uses;
static struct tag *tags;
static struct tag **next_tag = &tags;

unsigned long long str_to_llu_hash_helper(const char *str);

static void save_tag(struct position *pos, const char *name,
		     enum destination_type type, unsigned long long dest,
		     unsigned int dest_line, unsigned int dest_pos)
{
	struct tag *tag;

	tag = calloc(1, sizeof(*tag));
	if (!tag)
		die("out of memory\n");
	tag->identifier = strdup(name);
	if (!tag->identifier)
		die("out of memory\n");
	tag->file = str_to_llu_hash_helper(stream_name(pos->stream));
	tag->line = pos->line;
	tag->pos = pos->pos;
	tag->type = type;
	if (type == BASE) {
		tag->dest = tag->file;
		tag->dest_line = tag->line;
		tag->dest_pos = tag->pos;
	} else {
		tag->dest = dest;
		tag->dest_line = dest_line;
		tag->dest_pos = dest_pos;
	}
	*next_tag = tag;
	next_tag = &tag->next;
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

static int seen_macro(struct position *pos, const char *name)
{
	struct macro_use *use;

	for (use = macro_uses; use; use = use->next) {
		if (use->pos.stream == pos->stream &&
		    use->pos.line == pos->line && use->pos.pos == pos->pos &&
		    !strcmp(use->name, name))
			return 1;
	}

	use = malloc(sizeof(*use));
	if (!use)
		die("out of memory\n");
	use->pos = *pos;
	use->name = name;
	use->next = macro_uses;
	macro_uses = use;

	return 0;
}

static int same_position(struct position *one, struct position *two)
{
	return one->stream == two->stream && one->line == two->line &&
	       one->pos == two->pos;
}

static int show_macro(struct position *pos)
{
	struct symbol *sym;
	const char *name;

	name = get_macro_name(*pos);
	if (!name)
		return 0;
	sym = lookup_macro_symbol(name);
	if (!sym)
		return 0;
	if (seen_macro(pos, name))
		return 1;
	if (same_position(pos, &sym->pos)) {
		save_tag(pos, name, BASE, 0, 0, 0);
		return 1;
	}

	save_tag(pos, name, NORMAL,
		 str_to_llu_hash_helper(stream_name(sym->pos.stream)),
		 sym->pos.line, sym->pos.pos);
	return 1;
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

static void show_identifier(struct position *pos, struct symbol *sym)
{
	struct symbol *implementation;
	struct ident *ident;

	if (!sym)
		return;
	if (!source_position(pos) && !function_was_called(sym))
		return;
	if (show_macro(pos))
		return;
	ident = sym->ident;
	if (!ident || ident->reserved)
		return;
	implementation = get_implementation(sym);
	if (!implementation) {
		save_tag(pos, show_ident(ident), LOOKUP,
			 str_to_llu_hash_helper(ident->name), 0, 0);
		return;
	}
	if (same_position(pos, &implementation->pos)) {
		save_tag(pos, show_ident(ident), BASE, 0, 0, 0);
		return;
	}

	save_tag(pos, show_ident(ident), NORMAL,
		 str_to_llu_hash_helper(stream_name(implementation->pos.stream)),
		 implementation->pos.line, implementation->pos.pos);
}

static void encode_key(unsigned char *buf, unsigned long long file,
		       unsigned int line, unsigned int pos)
{
	int i;

	for (i = 0; i < 8; i++)
		buf[i] = file >> (56 - i * 8);
	for (i = 0; i < 4; i++) {
		buf[8 + i] = line >> (24 - i * 8);
		buf[12 + i] = pos >> (24 - i * 8);
	}
}

static int put_tag(DB_TXN *txn, DB *source, DB *destination, struct tag *tag)
{
	unsigned char source_key_buf[16];
	unsigned char dest_key_buf[16];
	char source_value_buf[256];
	char dest_value_buf[256];
	DBT source_key = { 0 };
	DBT source_value = { 0 };
	DBT dest_key = { 0 };
	DBT dest_value = { 0 };
	int ret;

	encode_key(source_key_buf, tag->file, tag->line, tag->pos);
	encode_key(dest_key_buf, tag->dest, tag->dest_line, tag->dest_pos);
	snprintf(source_value_buf, sizeof(source_value_buf),
		 "%d %s %llu %u %u", tag->type, tag->identifier,
		 tag->dest, tag->dest_line, tag->dest_pos);
	snprintf(dest_value_buf, sizeof(dest_value_buf),
		 "%d %s %llu %u %u", tag->type, tag->identifier,
		 tag->file, tag->line, tag->pos);
	source_key.data = source_key_buf;
	source_key.size = sizeof(source_key_buf);
	source_value.data = source_value_buf;
	source_value.size = strlen(source_value_buf) + 1;
	dest_key.data = dest_key_buf;
	dest_key.size = sizeof(dest_key_buf);
	dest_value.data = dest_value_buf;
	dest_value.size = strlen(dest_value_buf) + 1;

	ret = source->put(source, txn, &source_key, &source_value, 0);
	if (ret)
		return ret;
	ret = destination->put(destination, txn, &dest_key, &dest_value,
			       DB_NODUPDATA);
	if (ret == DB_KEYEXIST)
		return 0;
	return ret;
}

static int write_batch(DB_ENV *env, DB *source, DB *destination,
		       struct tag *first, struct tag *end)
{
	DB_TXN *txn;
	struct tag *tag;
	int ret;

retry:
	ret = env->txn_begin(env, NULL, &txn, 0);
	if (ret)
		return ret;
	for (tag = first; tag != end; tag = tag->next) {
		ret = put_tag(txn, source, destination, tag);
		if (ret)
			break;
	}
	if (!ret)
		ret = txn->commit(txn, 0);
	else
		txn->abort(txn);
	if (ret == DB_LOCK_DEADLOCK || ret == DB_LOCK_NOTGRANTED)
		goto retry;
	return ret;
}

static int write_tags(const char *db_dir)
{
	DB_ENV *env = NULL;
	DB *source = NULL;
	DB *destination = NULL;
	struct tag *first;
	struct tag *end;
	int count;
	int flags;
	int ret;

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
out:
	if (destination)
		destination->close(destination, 0);
	if (source)
		source->close(source, 0);
	if (env)
		env->close(env, 0);
	if (ret)
		fprintf(stderr, "tagger: %s: %s\n", db_dir, db_strerror(ret));
	return ret;
}

static void report_symbol_definition(struct symbol *sym)
{
	show_identifier(&sym->pos, sym);
}

static void report_member_definition(struct symbol *sym, struct symbol *member)
{
	show_identifier(&member->pos, member);
}

static void report_symbol(unsigned mode, struct position *pos,
			  struct symbol *sym)
{
	record_function_call(mode, sym);
	show_identifier(pos, sym);
}

static void report_member(unsigned mode, struct position *pos,
			  struct symbol *sym, struct symbol *member)
{
	show_identifier(pos, member);
}

int main(int argc, char **argv)
{
	static struct reporter reporter = {
		.r_follow = follow_function,
		.r_memdef = report_member_definition,
		.r_member = report_member,
		.r_symdef = report_symbol_definition,
		.r_symbol = report_symbol,
	};
	struct string_list *filelist = NULL;
	const char *db_dir;
	int i;

	if (argc < 2)
		die("usage: tagger <database directory> [sparse options] file.c\n");
	db_dir = argv[1];
	for (i = 1; i < argc - 1; i++)
		argv[i] = argv[i + 1];
	argc--;
	argv[argc] = NULL;

	sparse_initialize(argc, argv, &filelist);
	source_files = filelist;
	dissect_show_all_symbols = 1;
	dissect(&reporter, filelist);

	return write_tags(db_dir) != 0;
}
