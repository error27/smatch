/*
 * check-name: extract if statement inside nested typeof
 * check-command: extract $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-typeof-if-statement.c.extract
 * check-output-end
 */

struct map { int x; };
struct lock { int a; struct map dep_map; };
struct net { int pad[8]; struct lock l; int *p; };

int held(const struct map *m);
void warn(void);

#define deref(p) ({ typeof(*(p)) *_p = (p); _p; })
#define prot(p, n) ({ if (!held(&(&(n)->l)->dep_map)) warn(); (p); })

int *f(struct net *net)
{
	typeof(deref(prot(net->p, net))) x = 0;
	return x;
}
