extern int a, *ptr;

int a = 0;
int a = 1;

int *ptr = &a;
int *ptr = &a;

static void foo(void)
{
	int a = 0;
	int a = 1;

	int *ptr = &a;
	int *ptr = &a;
}

/*
 * check-name: duplicated-defs
 * check-known-to-fail
 *
 * check-error-start
dup-defs-local.c:4:5: error: symbol 'a' has multiple initializers (originally initialized at duplicated-defs.c:3)
dup-defs-local.c:7:5: error: symbol 'ptr' has multiple initializers (originally initialized at duplicated-defs.c:6)
dup-defs-local.c:12:13: error: symbol 'a' has multiple initializers (originally initialized at duplicated-defs.c:11)
dup-defs-local.c:15:13: error: symbol 'ptr' has multiple initializers (originally initialized at duplicated-defs.c:14)
 * check-error-end
 */
