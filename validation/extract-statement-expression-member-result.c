/*
 * check-name: extract member access on a statement expression member
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-statement-expression-member-result.c.extract.last
 * check-output-end
 */

struct user_namespace {
	int uid_map;
};

struct cred {
	struct user_namespace *user_ns;
};

extern struct cred *current_cred(void);

int *current_uid_map(void)
{
	return &(({ current_cred()->user_ns; }))->uid_map;
}
