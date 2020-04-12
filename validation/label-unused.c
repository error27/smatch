static void foo(void)
{
l:
	return;
}

static int bar(void)
{
	return  ({
l:
		;
		0;
	});
}

/*
 * check-name: label-unused
 * check-known-to-fail
 *
 * check-error-start
label-unused.c:3:1: warning: unused label 'l'
label-unused.c:10:1: warning: unused label 'l'
 * check-error-end
 */
