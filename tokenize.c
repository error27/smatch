/*
 * This is a really stupid C tokenizer, intended to run after the
 * preprocessor.
 *
 * A smart preprocessor would be integrated and pass the compiler
 * the tokenized input directly, but lacking that we just tokenize
 * the preprocessor output.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "lib.h"
#include "token.h"

#define EOF (-1)

int input_stream_nr = 0;
struct stream *input_streams;
static int input_streams_allocated;

const char *show_special(int val)
{
	static const char *combinations[] = COMBINATION_STRINGS;
	static char buffer[4];

	buffer[0] = val;
	buffer[1] = 0;
	if (val >= SPECIAL_BASE)
		strcpy(buffer, combinations[val - SPECIAL_BASE]);
	return buffer;
}

const char *show_ident(const struct ident *ident)
{
	static char buffer[256];
	sprintf(buffer, "%.*s", ident->len, ident->name);
	return buffer;
}

char *charstr(char *ptr, unsigned char c, unsigned char escape, unsigned char next)
{
	if (isprint(c)) {
		if (c == escape || c == '\\')
			*ptr++ = '\\';
		*ptr++ = c;
		return ptr;
	}
	*ptr++ = '\\';
	switch (c) {
	case '\n':
		*ptr++ = 'n';
		return ptr;
	case '\t':
		*ptr++ = 't';
		return ptr;
	}
	if (!isdigit(next))
		return ptr + sprintf(ptr, "%o", c);
		
	return ptr + sprintf(ptr, "%03o", c);
}

const char *show_token(const struct token *token)
{
	static char buffer[256];

	if (!token)
		return "<no token>";
	switch (token->type) {
	case TOKEN_ERROR:
		return "syntax error";

	case TOKEN_IDENT:
		return show_ident(token->ident);

	case TOKEN_STRING: {
		char *ptr;
		int i;
		struct string *string = token->string;

		ptr = buffer;
		*ptr++ = '"';
		for (i = 0; i < string->length-1; i++) {
			unsigned char *p = string->data + i;
			ptr = charstr(ptr, p[0], '"', p[1]);
		}
		*ptr++ = '"';
		*ptr = '\0';
		return buffer;
	}

	case TOKEN_INTEGER: {
		const char *p = token->integer;
		switch (*p) {
		case 'o':	// octal
		case 'x':	// hex
			buffer[0] = '0';
			strcpy(buffer+1, p+1);
			return buffer;
		default:
			return p;
		}
	}

	case TOKEN_FP:
		return token->fp;

	case TOKEN_SPECIAL:
		return show_special(token->special);

	case TOKEN_CHAR: {
		char *ptr = buffer;
		int c = token->character;
		*ptr++ = '\'';
		ptr = charstr(ptr, c, '\'', 0);
		*ptr++ = '\'';
		*ptr++ = '\0';
		return buffer;
	}
	
	default:
		return "WTF???";
	}
}

int init_stream(const char *name)
{
	int stream = input_stream_nr;

	if (stream >= input_streams_allocated) {
		int newalloc = stream * 4 / 3 + 10;
		input_streams = realloc(input_streams, newalloc * sizeof(struct stream));
		if (!input_streams)
			die("Unable to allocate more streams space");
		input_streams_allocated = newalloc;
	}
	input_streams[stream].name = name;
	input_stream_nr = stream+1;
	return stream;
}

struct token * alloc_token(int stream, int line, int pos)
{
	struct token *token = __alloc_token(0);
	token->line = line;
	token->pos = pos;
	token->stream = stream;
	return token;
}

#define BUFSIZE (4096)
typedef struct {
	int fd, line, pos, offset, size, newline;
	struct token **tokenlist;
	struct token *token;
	unsigned char buffer[BUFSIZE];
} action_t;

static int nextchar(action_t *action)
{
	int offset = action->offset;
	int size = action->size;
	int c;

	if (offset >= size) {
		size = read(action->fd, action->buffer, sizeof(action->buffer));
		if (size <= 0)
			return EOF;
		action->size = size;
		action->offset = 0;
		offset = 0;
	}
	c = action->buffer[offset];
	action->offset = offset + 1;
	action->pos++;
	if (c == '\n') {
		action->line++;
		action->newline = 1;
		action->pos = 0;
	}
	return c;
}

struct token eof_token_entry;

static void mark_eof(action_t *action, struct token *end_token)
{
	eof_token_entry.next = &eof_token_entry;
	eof_token_entry.newline = 1;
	if (!end_token)
		end_token =  &eof_token_entry;
	*action->tokenlist = end_token;
	action->tokenlist = NULL;
}

static void add_token(action_t *action)
{
	struct token *token = action->token;

	action->token = NULL;
	token->next = NULL;
	*action->tokenlist = token;
	action->tokenlist = &token->next;
}

static void drop_token(action_t *action)
{
	action->newline |= action->token->newline;
	action->token = NULL;
}

static int get_base_number(unsigned int base, char **p, int next, action_t *action)
{
	char *buf = *p;

	*buf++ = next;
	for (;;) {		
		unsigned int n;
		next = nextchar(action);
		n = hexval(next);
		if (n >= base)
			break;
		*buf++ = next;
	}
	*p = buf;
	return next;
}

static int do_integer(char *buffer, int len, int next, action_t *action)
{
	struct token *token = action->token;
	void *buf;
	
	while (next == 'u' || next == 'U' || next == 'l' || next == 'L') {
		buffer[len++] = next;
		next = nextchar(action);
	}
	buffer[len++] = '\0';
	buf = __alloc_bytes(len);
	memcpy(buf, buffer, len);
	token->type = TOKEN_INTEGER;
	token->integer = buf;
	add_token(action);
	return next;
}

static int get_one_number(int c, action_t *action)
{
	static char buffer[256];
	int next = nextchar(action);
	char *p = buffer;

	*p++ = c;
	switch (next) {
	case '0'...'7':
		if (c == '0') {
			buffer[0] = 'o';
			next = get_base_number(8, &p, next, action);
			break;
		}
		/* fallthrough */
	case '8'...'9':
		next = get_base_number(10, &p, next, action);
		break;
	case 'x': case 'X':
		if (c == '0') {
			buffer[0] = 'x';
			next = get_base_number(16, &p, next, action);
		}
	}
	return do_integer(buffer, p - buffer, next, action);
}

static int escapechar(int first, int type, action_t *action, int *valp)
{
	int next, value;

	next = nextchar(action);
	value = first;

	if (first == '\n')
		warn(action->token, "Newline in string or character constant");

	if (first == '\\' && next != EOF) {
		value = next;
		next = nextchar(action);
		if (value != type) {
			switch (value) {
			case 'n':
				value = '\n';
				break;
			case 't':
				value = '\t';
				break;
			case '\\':
				break;
			case '0'...'7': {
				int nr = 2;
				value -= '0';
				while (next >= '0' && next <= '9') {
					value = (value << 3) + (next-'0');
					next = nextchar(action);
					if (!--nr)
						break;
				}
				value &= 0xff;
				break;
			}
			case 'x': {
				int hex = hexval(next);
				if (hex < 16) {
					value = hex;
					next = nextchar(action);
					while ((hex = hexval(next)) < 16) {
						value = (value << 4) + hex;
						next = nextchar(action);
					}
					value &= 0xff;
					break;
				}
			}
			/* Fallthrough */
			default:
				warn(action->token, "Unknown escape '%c'", value);
			}
		}
		/* Mark it as escaped */
		value |= 0x100;
	}
	*valp = value;
	return next;
}

static int get_char_token(int next, action_t *action)
{
	int value;
	struct token *token;

	next = escapechar(next, '\'', action, &value);
	if (value == '\'' || next != '\'') {
		warn(action->token, "Bad character constant");
		drop_token(action);
		return next;
	}

	token = action->token;
	token->type = TOKEN_CHAR;
	token->character = value & 0xff;

	add_token(action);
	return nextchar(action);
}

static int get_string_token(int next, action_t *action)
{
	static char buffer[512];
	struct string *string;
	struct token *token;
	int len = 0;

	for (;;) {
		int val;
		next = escapechar(next, '"', action, &val);
		if (val == '"')
			break;
		if (next == EOF) {
			warn(action->token, "Enf of file in middle of string");
			return next;
		}
		if (len < sizeof(buffer)) {
			buffer[len] = val;
			len++;
		}
			
	}

	if (len > 256)
		warn(action->token, "String too long");

	string = __alloc_string(len+1);
	memcpy(string->data, buffer, len);
	string->data[len] = '\0';
	string->length = len+1;

	/* Pass it on.. */
	token = action->token;
	token->type = TOKEN_STRING;
	token->string = string;
	add_token(action);
	
	return next;
}

static int drop_stream_eoln(action_t *action)
{
	int next = nextchar(action);
	drop_token(action);
	for (;;) {
		int curr = next;
		if (curr == EOF)
			return next;
		next = nextchar(action);
		if (curr == '\n')
			return next;
	}
}

static int drop_stream_comment(action_t *action)
{
	int next = nextchar(action);
	drop_token(action);
	for (;;) {
		int curr = next;
		if (curr == EOF) {
			warn(action->token, "End of file in the middle of a comment");
			return curr;
		}
		next = nextchar(action);
		if (curr == '*' && next == '/')
			break;
	}
	return nextchar(action);
}

unsigned char combinations[][3] = COMBINATION_STRINGS;

#define NR_COMBINATIONS (sizeof(combinations)/3)

static int get_one_special(int c, action_t *action)
{
	struct token *token;
	unsigned char c1, c2, c3;
	int next, value, i;
	char *comb;

	next = nextchar(action);

	/*
	 * Check for strings, character constants, and comments
	 */
	switch (c) {
	case '"':
		return get_string_token(next, action);
	case '\'':
		return get_char_token(next, action);
	case '/':
		if (next == '/')
			return drop_stream_eoln(action);
		if (next == '*')
			return drop_stream_comment(action);
	}

	/*
	 * Check for combinations
	 */
	value = c;
	comb = combinations[0];
	c1 = c; c2 = next; c3 = 0;
	for (i = 0; i < NR_COMBINATIONS; i++) {
		if (comb[0] == c1 && comb[1] == c2 && comb[2] == c3) {
			value = i + SPECIAL_BASE;
			next = nextchar(action);
			if (c3)
				break;
			c3 = next;
		}
		comb += 3;
	}

	/* Pass it on.. */
	token = action->token;
	token->type = TOKEN_SPECIAL;
	token->special = value;
	add_token(action);
	return next;
}

#define IDENT_HASH_BITS (10)
#define IDENT_HASH_SIZE (1<<IDENT_HASH_BITS)
#define IDENT_HASH_MASK (IDENT_HASH_SIZE-1)

#define ident_hash_init(c)		(c)
#define ident_hash_add(oldhash,c)	((oldhash)*11 + (c))
#define ident_hash_end(hash)		((((hash) >> IDENT_HASH_BITS) + (hash)) & IDENT_HASH_MASK)

static struct ident *hash_table[IDENT_HASH_SIZE];
int ident_hit, ident_miss;

void show_identifier_stats(void)
{
	int i;
	int distribution[100];

	fprintf(stderr, "identifiers: %d hits, %d misses\n",
		ident_hit, ident_miss);

	for (i = 0; i < 100; i++)
		distribution[i] = 0;

	for (i = 0; i < IDENT_HASH_SIZE; i++) {
		struct ident * ident = hash_table[i];
		int count = 0;

		while (ident) {
			count++;
			ident = ident->next;
		}
		if (count > 99)
			count = 99;
		distribution[count]++;
	}

	for (i = 0; i < 100; i++) {
		if (distribution[i])
			fprintf(stderr, "%2d: %d buckets\n", i, distribution[i]);
	}
}

static struct ident *alloc_ident(const char *name, int len)
{
	struct ident *ident = __alloc_ident(len);
	ident->symbols = NULL;
	ident->len = len;
	memcpy(ident->name, name, len);
	return ident;
}

static struct ident * insert_hash(struct ident *ident, unsigned long hash)
{
	ident->next = hash_table[hash];
	hash_table[hash] = ident;
	ident_miss++;
	return ident;
}

static struct ident *create_hashed_ident(const char *name, int len, unsigned long hash)
{
	struct ident *ident;

	ident = hash_table[hash];
	while (ident) {
		if (ident->len == len && !memcmp(ident->name, name, len)) {
			ident_hit++;
			return ident;
		}
		ident = ident->next;
	}

	return insert_hash(alloc_ident(name, len), hash);
}

static unsigned long hash_name(const char *name, int len)
{
	unsigned long hash;
	const unsigned char *p = (const unsigned char *)name;

	hash = ident_hash_init(*p++);
	while (--len) {
		unsigned int i = *p++;
		hash = ident_hash_add(hash, i);
	}
	return ident_hash_end(hash);
}

struct ident *hash_ident(struct ident *ident)
{
	return insert_hash(ident, hash_name(ident->name, ident->len));
}

struct ident *built_in_ident(const char *name)
{
	int len = strlen(name);
	return create_hashed_ident(name, len, hash_name(name, len));
}

struct token *built_in_token(int stream, const char *name)
{
	struct token *token;

	token = alloc_token(stream, 0, 0);
	token->type = TOKEN_IDENT;
	token->ident = built_in_ident(name);
	return token;
}

static int get_one_identifier(int c, action_t *action)
{
	struct token *token;
	struct ident *ident;
	unsigned long hash;
	char buf[256];
	int len = 1;
	int next;

	hash = ident_hash_init(c);
	buf[0] = c;
	for (;;) {
		next = nextchar(action);
		switch (next) {
		case '0'...'9':
		case 'a'...'z':
		case 'A'...'Z':
		case '_':
			if (len < sizeof(buf)) {
				hash = ident_hash_add(hash, next);
				buf[len] = next;
				len++;
			}
			continue;
		}
		break;
	};
	hash = ident_hash_end(hash);

	ident = create_hashed_ident(buf, len, hash);

	/* Pass it on.. */
	token = action->token;
	token->type = TOKEN_IDENT;
	token->ident = ident;
	add_token(action);
	return next;
}		

static int get_one_token(int c, action_t *action)
{
	switch (c) {
	case '0'...'9':
		return get_one_number(c, action);
	case 'a'...'z':
	case 'A'...'Z':
	case '_':
		return get_one_identifier(c, action);
	default:
		return get_one_special(c, action);
	}	
}

struct token * tokenize(const char *name, int fd, struct token *endtoken)
{
	struct token *retval;
	int stream = init_stream(name);
	action_t action;
	int c;

	retval = NULL;
	action.tokenlist = &retval;
	action.token = NULL;
	action.line = 1;
	action.newline = 1;
	action.pos = 0;
	action.fd = fd;
	action.offset = 0;
	action.size = 0;

	c = nextchar(&action);
	while (c != EOF) {
		if (c == '\\') {
			c = nextchar(&action);
			action.newline = 0;
		}
		if (!isspace(c)) {
			struct token *token = alloc_token(stream, action.line, action.pos);
			token->newline = action.newline;
			action.newline = 0;
			action.token = token;
			c = get_one_token(c, &action);
			continue;
		}
		c = nextchar(&action);
	}
	mark_eof(&action, endtoken);
	return retval;
}
