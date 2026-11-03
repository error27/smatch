struct opaque;
struct inner { int a; };
struct outer { struct inner *in; long b[4]; };
struct byval { char c; int x; };
typedef struct outer outer_t;
enum e { E1 };

extern int ext(struct inner *in);

static int foo(outer_t *o, const struct inner *in, struct opaque *op, void *v,
	       int n, enum e e, char **pp, int (*fn)(int), struct byval bv,
	       unsigned long arr[4], int flex[], ...)
{
	(void)op; (void)v; (void)pp; (void)fn; (void)arr; (void)flex;
	return ext((struct inner *)in) + n + e + bv.x + (o ? 1 : 0);
}

/*
 * check-name: extract-main
 * check-command: extract --main --func=foo $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract.foo && grep -A 100 "^struct outer {" $file.extract.foo
 *
 * check-output-start
extract: wrote extract-main.c.extract.foo
struct outer {
	struct inner *in;
	long b[4];
};
struct inner {
	int a;
};
int main(void)
{
	static __typeof__(struct outer) __extract_arg0;
	static __typeof__(struct inner) __extract_arg1;
	static char __extract_arg2[4096] __attribute__((aligned(64)));
	static char __extract_arg3[4096] __attribute__((aligned(64)));
	static __typeof__(char *) __extract_arg6;
	static __typeof__(struct byval) __extract_arg8;
	static __typeof__(unsigned long [4]) __extract_arg9;
	static __typeof__(int) __extract_arg10;

	foo(&__extract_arg0, &__extract_arg1, (void *)__extract_arg2, (void *)__extract_arg3, 0, 0, &__extract_arg6, 0, __extract_arg8, __extract_arg9, &__extract_arg10);
	return 0;
}

 * check-output-end
 */
