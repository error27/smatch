extern int i;

int foo(void)
{
	return *i;
}

int bar(void)
{
	return i[0];
}

int *qux(void)
{
	return &i[0];
}

/*
 * check-name: premature-examination
 * check-command: sparse -Wno-decl $file
 *
 * check-error-start
eval/premature-examination.c:5:16: error: cannot dereference this type
eval/premature-examination.c:10:17: error: cannot dereference this type
eval/premature-examination.c:15:18: error: cannot dereference this type
 * check-error-end
 */
