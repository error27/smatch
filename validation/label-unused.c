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

static void baz(void)
{
l: __attribute__((unused));
	return;
}

/*
 * check-name: label-unused
 *
 * check-error-start
label-unused.c:3:1: warning: unused label 'l'
label-unused.c:10:1: warning: unused label 'l'
 * check-error-end
 */
