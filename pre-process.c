/*
 * Do C preprocessing, based on a token list gathered by
 * the tokenizer.
 *
 * This may not be the smartest preprocessor on the planet.
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
#include "symbol.h"

static struct token *preprocessor_line(struct token * head)
{
	struct token *token = head->next;	// hash-mark

	do {
		fprintf(stderr, "%s ", show_token(token));
		token = token->next;
	} while (!token->newline && !eof_token(token));
	fprintf(stderr, "\n");
	head->next = token;
	return head;
}

static void do_preprocess(struct token *head)
{
	do {
		struct token *next = head->next;
		if (next->newline && match_op(next, '#')) {
			head = preprocessor_line(head);
			continue;
		}
		head = next;
	} while (!eof_token(head));
}

struct token * preprocess(struct token *token)
{
	struct token header = { 0, };

	header.next = token;
	do_preprocess(&header);
	return header.next;
}
