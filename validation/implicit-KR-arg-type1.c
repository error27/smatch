static int foo(a, b)
	int a;
{
	if (b)
		return a;
}

static int bar(a)
{
	if (a)
		return a;
}

/*
 * check-name: implicit-KR-arg-type1
 * check-command: sparse -Wold-style-definition -Wimplicit-int $file
 *
 * check-error-start
implicit-KR-arg-type1.c:2:9: warning: non-ANSI definition of function 'foo'
implicit-KR-arg-type1.c:1:19: warning: missing type declaration for parameter 'b'
implicit-KR-arg-type1.c:8:16: error: missing type declaration for parameter 'a'
 * check-error-end
 */
