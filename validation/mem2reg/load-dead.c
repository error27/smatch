int fun(int);

static inline int fake(void)
{
}

static void foo(int a)
{
	0 || fun((a, fake(), a));
}

/*
 * check-name: load-dead
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-excludes: VOID
 */
