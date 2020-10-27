extern void use(void *);

static inline int inl0(int a);
static inline int inl1(int a);

static inline int inl0(int a)
{
	return a;
}

void foo(void)
{
	use(inl0);
	use(inl1);
}

static inline int inl1(int a)
{
	return a;
}

/*
 * check-name: inline-definition
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-contains: inl0:
 * check-output-contains: inl1:
 */
