static void a(void) __attribute__ ((context(A, 0, 1)))
{
    __context__(A, 1);
}

static void r(void) __attribute__ ((context(A, 1, 0)))
{
    __context__(A, -1);
}

extern int condition, condition2;

static int tl(void) __attribute__ ((conditional_context(A, 0, 1, 0)))
{
    if (condition) {
        a();
        return 1;
    }
    return 0;
}

static int tl2(void) __attribute__ ((conditional_context(A, 0, 0, 1)))
{
    if (condition) {
        a();
        return 1;
    }
    return 0;
}

static int dummy(void)
{
    return condition + condition2;
}

static int good_trylock1(void)
{
    if (tl()) {
        r();
    }
}

static int good_trylock2(void)
{
    if (tl()) {
        r();
    }

    if (tl()) {
        r();
    }
}
static int good_trylock3(void)
{
    a();
    if (tl()) {
        r();
    }
    r();
    if (tl()) {
        r();
    }
}

static int good_trylock4(void)
{
    a();
    if (tl()) {
        r();
    }
    if (tl()) {
        r();
    }
    r();
}

static void bad_trylock1(void)
{
    a();
    if (dummy()) {
        r();
    }
    r();
}

static int good_trylock5(void)
{
    if (!tl2()) {
        r();
    }
}

static int good_trylock6(void)
{
    if (!tl2()) {
        r();
    }

    if (!tl2()) {
        r();
    }
}
static int good_trylock7(void)
{
    a();
    if (!tl2()) {
        r();
    }
    r();
    if (!tl2()) {
        r();
    }
}

static int good_trylock8(void)
{
    a();
    if (!tl2()) {
        r();
    }
    if (!tl2()) {
        r();
    }
    r();
}

static void bad_trylock2(void)
{
    a();
    if (!dummy()) {
        r();
    }
    r();
}

static int good_switch(void)
{
    switch (condition) {
    case 1:
        a();
        break;
    case 2:
        a();
        break;
    case 3:
        a();
        break;
    default:
        a();
    }
    r();
}

static void bad_lock1(void)
{
    r();
    a();
}

/*
 * check-name: Check -Wcontext with lock trylocks
 *
 * check-error-start
context-dynamic.c:83:6: warning: context problem in 'bad_trylock1': 'r' expected different context
context-dynamic.c:83:6:    context 'A': wanted >= 1, got 0
context-dynamic.c:133:6: warning: context problem in 'bad_trylock2': 'r' expected different context
context-dynamic.c:133:6:    context 'A': wanted >= 1, got 0
context-dynamic.c:156:6: warning: context problem in 'bad_lock1': 'r' expected different context
context-dynamic.c:156:6:    context 'A': wanted >= 1, got 0
 * check-error-end
 */
