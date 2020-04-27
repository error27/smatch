void foo(void)
{
	void *p = &&l;
	{
l:		 ;
	}
	goto *p;			// OK
}

void bar(void)
{
	void *p = &&l;			// KO: 'jump' inside
	({
l:		 1;
	});
	goto *p;
}

void baz(void)
{
	void *p = &&l;			// KO: 'jump' inside
	0 ? 1 : ({
l:		 1;
		 });
	goto *p;
}

void qux(void)
{
	void *p = &&l;			// KO: 'jump' inside + removed
	1 ? 1 : ({
l:		 1;
		 });
	goto *p;
}

void quz(void)
{
	void *p;
	p = &&l;			// KO: undeclared
	goto *p;
}

void qxu(void)
{
	void *p;
	({
l:		1;
	 });
	p = &&l;			// KO: 'jump' inside
	goto *p;
}

void qzu(void)
{
	void *p;
	1 ? 1 : ({
l:		 1;
		 });
	p = &&l;			// KO: 'jump' inside + removed
	goto *p;
}


/*
 * check-name: label-scope-cgoto
 * check-command: sparse -Wno-decl $file
 *
 * check-error-start
label-scope-cgoto.c:12:19: error: label 'l' used outside statement expression
label-scope-cgoto.c:14:1:    label 'l' defined here
label-scope-cgoto.c:21:19: error: label 'l' used outside statement expression
label-scope-cgoto.c:23:1:    label 'l' defined here
label-scope-cgoto.c:30:19: error: label 'l' used outside statement expression
label-scope-cgoto.c:32:1:    label 'l' defined here
label-scope-cgoto.c:50:13: error: label 'l' used outside statement expression
label-scope-cgoto.c:48:1:    label 'l' defined here
label-scope-cgoto.c:60:13: error: label 'l' used outside statement expression
label-scope-cgoto.c:58:1:    label 'l' defined here
label-scope-cgoto.c:40:13: error: label 'l' was not declared
 * check-error-end
 */
