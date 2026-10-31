/*
 * check-name: extract struct assignment through dereferenced pointers
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-struct-assignment-deref.c.extract.last
 * check-output-end
 */

struct pktinfo { int a[3]; };
struct buf { char cb[48]; };

void copy_info(struct buf *dst, struct buf *src)
{
	*((struct pktinfo *)dst->cb) = *((struct pktinfo *)src->cb);
}
