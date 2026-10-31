/*
 * check-name: extract typeof of a function-typed parameter
 * check-command: extract $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-function-parameter-typeof.c.extract
 * check-output-end
 */

extern int f1(int x);

int call(int getfrag(int x), int x)
{
	int local_fn(int);

	return ({ typeof(getfrag) __f1 = (getfrag); __f1 == f1 ? f1(x) : __f1(x); }) + local_fn(x);
}
