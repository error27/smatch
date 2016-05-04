int op(int);

static int good(void)
{
	__context__(1);
	for (int i = 0; i < 10; i++) {
		if (!op(i)) {
			__context__(-1);
			return 0;
		}
	}
	__context__(-1);
	return 1;
}

static int bad(void)
{
	__context__(1);
	for (int i = 0; i < 10; i++) {
		if (!op(i)) {
			__context__(-1);
			return 0;
		}
	}
	return 1;
}
/*
 * check-name: C99 for loop variable declaration
 *
 * check-error-start
c99-for-loop.c:16:12: warning: context imbalance in 'bad' - different lock contexts for basic block
 * check-error-end
 */
