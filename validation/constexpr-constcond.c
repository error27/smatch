extern int var;

static int a[] = {
	[0 ? var : 1] = 0,
	[1 ? 2 : var] = 0,
};

/*
 * check-name: constexprness in constant conditionals
 */
