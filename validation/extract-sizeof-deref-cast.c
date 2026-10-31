/*
 * check-name: extract sizeof a dereferenced pointer cast
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-sizeof-deref-cast.c.extract.last
 * check-output-end
 */

struct parm { int a[4]; };
struct buf { char cb[48]; };

unsigned long parm_size(struct buf *b)
{
	return sizeof(*((struct parm *)b->cb));
}
