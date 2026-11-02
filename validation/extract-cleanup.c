struct lock { int held; };

static inline void unlock(struct lock **l)
{
	(*l)->held = 0;
}

int test(struct lock *lock)
{
	struct lock *guard __attribute__((__cleanup__(unlock))) = lock;

	lock->held = 1;
	return 0;
}

/*
 * check-name: extract keeps the cleanup attribute
 * check-command: extract --func=test $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-cleanup.c.extract.test
 * check-output-end
 */
