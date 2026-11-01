struct ops;

struct match_token { int token; const char *pattern; };
typedef struct match_token match_table_t[];

struct ops {
	const match_table_t *tokens;
};

int test(struct ops *ops)
{
	return ops->tokens != 0;
}

/*
 * check-name: extract a pointer to an array of structs as a member
 * check-command: extract $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-pointer-to-array-member.c.extract
 * check-output-end
 */
