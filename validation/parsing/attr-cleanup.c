#define __cleanup(F)	__attribute__((__cleanup__(F)))

void fun(int *ptr);

int test(int n);
int test(int n)
{
	int var __attribute__((cleanup(fun))) = 1;
	int alt __cleanup(fun) = 2;
	int mis __cleanup(0) = 3;
	int non __attribute__((cleanup));
	int mis __attribute__((cleanup()));
	int two __attribute__((cleanup(fun, fun)));

        for (int i __cleanup(fun) = 0; i < n; i++)
		;

	var = 5;
	return 0;
}

/*
 * check-name: attr-cleanup
 * check-command: sparse -Wunknown-attribute $file
 *
 * check-error-start
parsing/attr-cleanup.c:10:17: error: argument is not an identifier
parsing/attr-cleanup.c:11:39: error: an argument is expected for attribute 'cleanup'
parsing/attr-cleanup.c:12:40: error: an argument is expected for attribute 'cleanup'
parsing/attr-cleanup.c:13:43: error: Expected ) after attribute's argument'
parsing/attr-cleanup.c:13:43: error: got ,
 * check-error-end
 */
