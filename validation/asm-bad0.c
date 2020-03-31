extern char string[];
extern int *var;

static void templ(void)
{
	asm(string);
}

static void ocons(void)
{
	asm("template" : [out] string (var) : [in] "r" (0));
}

static void icons(void)
{
	asm("template" : [out] "=r" (var): [in] string (0));
}

static void oexpr(void)
{
	asm("template" : [out] "=" (var[) : [in] "r" (0));
}

static void iexpr(void)
{
	asm("template" : [out] "=r" (var) : [in] "r" (var[));
}

/*
 * check-name: asm-bad0
 *
 * check-error-start
asm-bad0.c:6:13: error: string literal expected for inline asm
asm-bad0.c:11:32: error: string literal expected for asm constraint
asm-bad0.c:16:49: error: string literal expected for asm constraint
asm-bad0.c:21:41: error: Expected ] at end of array dereference
asm-bad0.c:21:41: error: got )
asm-bad0.c:26:59: error: Expected ] at end of array dereference
asm-bad0.c:26:59: error: got )
 * check-error-end
 */
