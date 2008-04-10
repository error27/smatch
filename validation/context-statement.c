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

/*
 * check-name: Check __context__ statement with required context
 *
 * check-error-start
context-statement.c:16:8: warning: context imbalance in 'bad_arr' - unexpected unlock (LOCK)
context-statement.c:38:5: warning: context imbalance in 'bad_macro1' - __context__ statement expected different lock context (LOCK)
context-statement.c:47:5: warning: context imbalance in 'bad_macro2' - __context__ statement expected different lock context (LOCK)
 * check-error-end
 */
