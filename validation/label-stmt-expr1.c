static int foo(void)
{
	goto l;
	({
l:
		0;
	});
}

static void bar(void)
{
	({
l:
		0;
	});
	goto l;
}

/*
 * check-name: label-stmt-expr1
 *
 * check-error-start
label-stmt-expr1.c:3:9: error: label 'l' used outside statement expression
label-stmt-expr1.c:5:1:    label 'l' defined here
label-stmt-expr1.c:16:9: error: label 'l' used outside statement expression
label-stmt-expr1.c:13:1:    label 'l' defined here
 * check-error-end
 */
