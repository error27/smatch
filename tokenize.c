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
#include "token.h"

#define EOF (-1)

int input_stream_nr = 0;
struct stream *input_streams;
static int input_streams_allocated;

static int init_stream(const char *name)
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

#define BUFSIZE (4096)
typedef struct {
	int fd, line, pos, offset, size;
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
		action->pos = 0;
	}
	return c;
}

static void warn(action_t *action, const char *fmt, ...)
{
	static char buffer[512];
	struct stream *stream;
	struct token *token = action->token;

	va_list args;
	va_start(args, fmt);
	vsprintf(buffer, fmt, args);
	va_end(args);

	stream = input_streams + token->stream;
	fprintf(stderr, "warning: %s:%d: %s\n",
		stream->name, token->line,
		buffer);
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
	struct token *token = action->token;

	action->token = NULL;
	free(token);
}

static int do_integer(unsigned long long value, int next, action_t *action)
{
	struct token *token = action->token;
	
	token->value.type = TOKEN_INTEGER;
	token->value.intval = value;
	add_token(action);
	return next;
}

static int get_base_number(unsigned int base, unsigned int __val, action_t *action)
{
	unsigned long long value = __val;
	int next;

	for (;;) {
		unsigned int n = 1000;
		next = nextchar(action);
		switch (next) {
		case '0'...'9':
			n = next-'0';
			break;
		case 'a'...'z':
			n = next-'a'+10;
			break;
		case 'A'...'Z':
			n = next-'A'+10;
		}
		if (n >= base)
			break;
		value = value * base + n;
	}
	return do_integer(value, next, action);
}

/* Parse error: return a token with a NULL "value" part */
static void parse_error(action_t *action)
{
	add_token(action);
}

static int get_hex_number(int x, action_t *action)
{
	int next = nextchar(action);

	switch (next) {
	case '0'...'9':
		return get_base_number(16, next-'0', action);
	case 'a'...'f':		
		return get_base_number(16, next-'a'+10, action);
	case 'A'...'F':
		return get_base_number(16, next-'A'+10, action);
	}
	parse_error(action);
	return next;
}

static int get_one_number(int c, action_t *action)
{
	int next = nextchar(action);

	switch (next) {
	case '0'...'7':
		if (c == '0')
			return get_base_number(8, next-'0', action);
		/* fallthrough */
	case '8'...'9':
		return get_base_number(10, c*10+next-'0'*11, action);
	case 'x': case 'X':
		if (c == '0')
			return get_hex_number(next, action);
	}
	return do_integer(c-'0', next, action);
}

static int hexval(int c)
{
	int retval = 256;
	switch (c) {
	case '0'...'9':
		retval = c - '0';
		break;
	case 'a'...'f':
		retval = c - 'a' + 10;
		break;
	case 'A'...'F':
		retval = c - 'A' + 10;
		break;
	}
	return retval;
}

static int escapechar(int first, int type, action_t *action, int *valp)
{
	int next, value;

	next = nextchar(action);
	value = first;

	if (first == '\n')
		warn(action, "Newline in string or character constant");

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
				warn(action, "Unknown escape '%c'", value);
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
		warn(action, "Bad character constant");
		drop_token(action);
		return next;
	}

	token = action->token;
	token->value.type = TOKEN_INTEGER;
	token->value.intval = value & 0xff;

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
			warn(action, "Enf of file in middle of string");
			return next;
		}
		if (len < sizeof(buffer)) {
			buffer[len] = val;
			len++;
		}
			
	}

	if (len > 256)
		warn(action, "String too long");

	string = malloc(sizeof(int)+len);
	memcpy(string->data, buffer, len);
	string->length = len;

	/* Pass it on.. */
	token = action->token;
	token->value.type = TOKEN_STRING;
	token->value.string = string;
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
			warn(action, "End of file in the middle of a comment");
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
			c = next;
			next = nextchar(action);
			if (c3)
				break;
			c3 = c;
		}
		comb += 3;
	}

	/* Pass it on.. */
	token = action->token;
	token->value.type = TOKEN_SPECIAL;
	token->value.special = value;
	add_token(action);
	return next;
}

#define IDENT_HASH_BITS (10)
#define IDENT_HASH_SIZE (1<<IDENT_HASH_BITS)
#define IDENT_HASH_MASK (IDENT_HASH_SIZE-1)
static struct ident *hash_table[IDENT_HASH_SIZE];
int ident_hit, ident_miss;

void print_ident_stat(void)
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

static struct ident *create_hashed_ident(const char *name, int len, unsigned long hash)
{
	struct ident *ident;

	hash = ((hash >> IDENT_HASH_BITS) + hash) & IDENT_HASH_MASK;
	ident = hash_table[hash];
	while (ident) {
		if (ident->len == len && !memcmp(ident->name, name, len)) {
			ident_hit++;
			return ident;
		}
		ident = ident->next;
	}

	ident = malloc(offsetof(struct ident,name) + len);
	if (!ident)
		die("Out of memory for identifiers");

	ident->symbol = NULL;
	ident->len = len;
	memcpy(ident->name, name, len);
	ident->next = hash_table[hash];
	hash_table[hash] = ident;
	ident_miss++;
	return ident;
}

#define ident_hash_init(c)		(c)
#define ident_hash_add(oldhash,c)	((oldhash)*11 + (c))
#define ident_hash_end(hash)		(hash)

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
	token->value.type = TOKEN_IDENT;
	token->value.ident = ident;
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

struct token * tokenize(const char *name, int fd)
{
	struct token *retval;
	int stream = init_stream(name);
	action_t action;
	int c;

	retval = NULL;
	action.tokenlist = &retval;
	action.token = NULL;
	action.line = 1;
	action.pos = 0;
	action.fd = fd;
	action.offset = 0;
	action.size = 0;

	c = nextchar(&action);
	while (c != EOF) {
		if (!isspace(c)) {
			struct token *token = malloc(sizeof(struct token));
			if (!token)
				die("Out of memory for token");

			memset(token, 0, sizeof(struct token));
			token->line = action.line;
			token->pos = action.pos;
			token->stream = stream;

			action.token = token;

			c = get_one_token(c, &action);
			continue;
		}
		c = nextchar(&action);
	}
	return retval;
}
