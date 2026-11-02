struct rcu_head { void *next; };
struct ac { unsigned long *user_mask; };

void test(struct ac ac)
{
	union rcuhead {
		unsigned long mask;
		struct rcu_head rcu;
	};

	do {
		typeof((union rcuhead *)ac.user_mask) ___p = (union rcuhead *)ac.user_mask;

		if (__builtin_offsetof(typeof(*((union rcuhead *)ac.user_mask)), rcu) >= 0x1000)
			return;
		(void)&(___p)->rcu;
	} while (0);
}

/*
 * check-name: extract a local union which is only used through typeof()
 * check-command: extract --func=test $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-local-tag-through-typeof.c.extract.test
 * check-output-end
 */
