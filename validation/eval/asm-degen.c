#ifdef __CHECKER__
#define __percpu __attribute__((noderef))
#else
#define __percpu
#endif

static __percpu int var;
static __percpu int arr[4];

static void foo(void)
{
	asm("" :: "r" (var));
}

static void bar(void)
{
	asm("" :: "r" (arr));
}

static void baz(void)
{
	asm("" :: "m" (var));
}

static void qux(void)
{
	asm("" :: "m" (arr));
}

/*
 * check-name: asm-degen
 *
 * check-error-start
eval/asm-degen.c:12:24: warning: dereference of noderef expression
 * check-error-end
 */
