/*
 * check-name: extract self-referencing typedef struct with the same name
 * check-command: extract $file && gcc -std=gnu11 -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-typedef-struct-self-reference.c.extract
 * check-output-end
 */

typedef struct ucontext_t {
	unsigned long uc_flags;
	struct ucontext_t *uc_link;
} ucontext_t;

int flags(ucontext_t *uc)
{
	return uc->uc_link->uc_flags;
}
