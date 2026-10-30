/*
 * check-name: extract _Generic member result
 * check-command: extract --last-func $file && gcc -std=gnu11 -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-generic-member-result.c.extract.last
 * check-output-end
 */

struct addr { int len; char path[8]; };
struct sock { int x; };
struct usock { struct sock sk; struct addr *addr; };

#define usk(s) _Generic(s, const struct sock *: ((const struct usock *)(s)), default: ((struct usock *)(s)))

static inline int has_path(struct sock *sk)
{
	return usk(sk)->addr && usk(sk)->addr->path[0];
}

int check(struct sock *sk)
{
	return has_path(sk);
}
