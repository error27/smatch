/*
 * check-name: extract _Generic inside typeof
 * check-command: extract $file && gcc -std=gnu11 -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-typeof-generic.c.extract
 * check-output-end
 */

struct sock { int x; };
struct req { long pad[3]; struct sock *sk; };

int get(struct req *req)
{
	typeof(_Generic(req->sk, const struct sock *: 1, default: req->sk->x)) val;

	val = req->sk->x;
	return val;
}
