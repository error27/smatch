static void ok_lvl2(void)
{
	__label__ l;

	{
	l:
		goto l;
	}
}

static void ko_expr2(void)
{
	{
		__label__ a;

		({
a:
			 0;
		});
		goto a;
	}
}

/*
 * check-name: label-scope2
 *
 * check-error-start
label-scope2.c:20:17: error: label 'a' used outside statement expression
label-scope2.c:17:1:    label 'a' defined here
 * check-error-end
 */
