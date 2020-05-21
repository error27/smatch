static void ok_top(void)
{
	__label__ l;
l:
	goto l;
}

static void ko_undecl(void)
{
	__label__ l;
	goto l;				// KO: undeclared
}

static void ok_local(void)
{
l:
	{
		__label__ l;
l:
		goto l;
	}
goto l;
}

static void ko_scope(void)
{
	{
		__label__ l;
l:
		goto l;
	}
goto l;					// KO: undeclared
}

/*
 * check-name: label-scope1
 *
 * check-error-start
label-scope1.c:11:9: error: label 'l' was not declared
label-scope1.c:32:1: error: label 'l' was not declared
 * check-error-end
 */
