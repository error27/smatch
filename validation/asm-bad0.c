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

static void oexpr(oid)
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
asm-bad0.c:21:41: error: Expected ] at end of array dereference
asm-bad0.c:21:41: error: got )
asm-bad0.c:26:59: error: Expected ] at end of array dereference
asm-bad0.c:26:59: error: got )
asm-bad0.c:6:9: error: need constant string for inline asm
asm-bad0.c:11:32: error: asm output constraint is not a string
asm-bad0.c:16:49: error: asm input constraint is not a string
 * check-error-end
 */
