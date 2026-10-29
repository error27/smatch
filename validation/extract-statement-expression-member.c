/*
 * check-name: extract statement expression member type
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-statement-expression-member.c.extract.last
 * check-output-end
 */

struct cred {
	int euid;
};

struct task {
	struct cred *real_cred;
};

extern struct task *get_current(void);

#define current_cred(ptr) ({ typeof(*(ptr)) *__ptr = (ptr); __ptr; })

int target(void)
{
	return current_cred(get_current()->real_cred)->euid;
}
