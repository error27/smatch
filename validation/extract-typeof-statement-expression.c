struct rcu_head { void *next; };
struct bind {
	int x;
	struct rcu_head rcu;
};

int test(struct bind *new)
{
	return __builtin_offsetof(typeof(*({ typeof(new) __tmp = new; __tmp; })), rcu);
}

/*
 * check-name: extract typeof() of a statement expression
 * check-command: extract --func=test $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-typeof-statement-expression.c.extract.test
 * check-output-end
 */
