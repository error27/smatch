#define a() __context__(LOCK, 1)
#define r() __context__(LOCK, -1)
#define m() __context__(LOCK, 0, 1)
#define m2() __context__(LOCK, 0, 2)

static void good_ar(void)
{
    a();
    r();
}

static void bad_arr(void)
{
    a();
    r();
    r();
}

static void good_macro1(void)
{
    a();
    m();
    r();
}

static void good_macro2(void)
{
    a();
    a();
    m();
    m2();
    r();
    r();
}

static void bad_macro1(void)
{
    m();
    a();
    r();
}

static void bad_macro2(void)
{
    a();
    r();
    m();
}

static void bad_macro3(void)
{
    r();
    a();
}

/*
 * check-name: Check __context__ statement with required context
 *
 * check-error-start
context-statement.c:16:8: warning: context imbalance in 'bad_arr': unexpected unlock
context-statement.c:16:8:    context 'LOCK': wanted 0, got -1
context-statement.c:38:5: warning: context imbalance in 'bad_macro1': __context__ statement expected different context
context-statement.c:38:5:    context 'LOCK': wanted >= 1, got 0
context-statement.c:47:5: warning: context imbalance in 'bad_macro2': __context__ statement expected different context
context-statement.c:47:5:    context 'LOCK': wanted >= 1, got 0
context-statement.c:53:5: warning: context imbalance in 'bad_macro3': __context__ statement expected different context
context-statement.c:53:5:    context 'LOCK': wanted >= 0, got -1
 * check-error-end
 */
