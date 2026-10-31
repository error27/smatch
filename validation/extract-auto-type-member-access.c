/*
 * check-name: extract member access through an __auto_type local
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-auto-type-member-access.c.extract.last
 * check-output-end
 */

struct bucket { int lock; };
struct ref { struct bucket *hb; };

int get_lock(struct ref r)
{
	__auto_type hb = r.hb;

	return hb->lock;
}
