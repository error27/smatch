static int foo(void)
{
	goto l;
	({
l:
		0;
	});
	goto l;
}

static void bar(void)
{
	goto l;
	goto l;
	({
l:
		0;
	});
}

static void baz(void)
{
	({
l:
		0;
	});
	goto l;
	goto l;
}

/*
 * check-name: label-stmt-expr2
 *
 * check-error-start
label-stmt-expr2.c:3:9: error: label 'l' used outside statement expression
label-stmt-expr2.c:5:1:    label 'l' defined here
label-stmt-expr2.c:8:9: error: label 'l' used outside statement expression
label-stmt-expr2.c:5:1:    label 'l' defined here
label-stmt-expr2.c:13:9: error: label 'l' used outside statement expression
label-stmt-expr2.c:16:1:    label 'l' defined here
label-stmt-expr2.c:27:9: error: label 'l' used outside statement expression
label-stmt-expr2.c:24:1:    label 'l' defined here
label-stmt-expr2.c:28:9: error: label 'l' used outside statement expression
label-stmt-expr2.c:24:1:    label 'l' defined here
 * check-error-end
 */
