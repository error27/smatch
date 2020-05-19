static int test_ok(int a, int b)
{
	int nested_ok(int i)
	{
		return i * 2;
	}
	return nested_ok(b);
}

static int test_ko(int a, int b)
{
	int nested_ko(int i)
	{
		return i * 2 + a;
	}
	return nested_ko(b);
}

static int test_inline(int a, int b)
{
	inline int nested(int i)
	{
		return i * 2;
	}
	return nested(b);
}

static int test_inline_ko(int a, int b)
{
	inline int nested(int i)
	{
		return i * 2 + a;
	}
	return nested(b);
}

/*
 * check-name: nested-functions
 *
 * check-error-start
nested-functions.c:32:32: warning: unreplaced symbol 'a'
 * check-error-end
 */
