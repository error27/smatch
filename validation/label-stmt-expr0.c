void aft(void)
{
	({
l:		 1;
	});
	goto l;				// KO
}

void bef(void)
{
	goto l;				// KO
	({
l:		 1;
	});
}

void lab(void)
{
	__label__ l;
	({
l:		 1;
	});
	goto l;				// KO
}

/*
 * check-name: label-stmt-expr0
 * check-command: sparse -Wno-decl $file
 *
 * check-error-start
label-stmt-expr0.c:6:9: error: label 'l' used outside statement expression
label-stmt-expr0.c:4:1:    label 'l' defined here
label-stmt-expr0.c:11:9: error: label 'l' used outside statement expression
label-stmt-expr0.c:13:1:    label 'l' defined here
label-stmt-expr0.c:23:9: error: label 'l' used outside statement expression
label-stmt-expr0.c:21:1:    label 'l' defined here
 * check-error-end
 */
