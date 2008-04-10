static void a(void) __attribute__((context(TEST,0,1)))
{
	__context__(TEST,1);
}

static void r(void) __attribute__((context(TEST,1,0)))
{
	__context__(TEST,-1,1);
}

static void a2(void) __attribute__((context(TEST2,0,1)))
{
	__context__(TEST2,1);
}

static void r2(void) __attribute__((context(TEST2,1,0)))
{
	__context__(TEST2,-1,1);
}

#define check_test2() __context__(TEST2,0,1)

static void good_paired1(void)
{
	a();
	a2();
	r();
	r2();
}

static void good_paired2(void)
{
	a();
	r();
	a();
	r();
	a2();
	r2();
}

static void good_paired3(void)
{
	a();
	a();
	r();
	r();
	a2();
	a2();
	r2();
	r2();
}

static void good_lock1(void) __attribute__((context(TEST,0,1)))
{
	a();
}

static void good_lock2(void) __attribute__((context(TEST,0,1)))
{
	a();
	r();
	a();
}

static void good_lock3(void) __attribute__((context(TEST,0,1)))
{
	a();
	a();
	r();
}

static void good_unlock1(void) __attribute__((context(TEST,1,0)))
{
	r();
}

static void good_unlock2(void) __attribute__((context(TEST,1,0)))
{
	a();
	r();
	r();
}

static void warn_lock1(void)
{
	a();
}

static void warn_lock2(void)
{
	a();
	r();
	a();
}

static void warn_lock3(void)
{
	a();
	a();
	r();
}

static void warn_unlock1(void)
{
	r();
}

static void warn_unlock2(void)
{
	a();
	r();
	r();
}

extern int condition, condition2;

static int good_if1(void)
{
	a();
	if(condition) {
		r();
		return -1;
	}
	r();
	return 0;
}

static void good_if2(void)
{
	if(condition) {
		a();
		r();
	}
}

static void good_if3(void)
{
	a();
	if(condition) {
		a();
		r();
	}
	r();
}

static int warn_if1(void)
{
	a();
	if(condition)
		return -1;
	r();
	return 0;
}

static int warn_if2(void)
{
	a();
	if(condition) {
		r();
		return -1;
	}
	return 0;
}

static void good_while1(void)
{
	a();
	while(condition)
		;
	r();
}

static void good_while2(void)
{
	while(condition) {
		a();
		r();
	}
}

static void good_while3(void)
{
	while(condition) {
		a();
		r();
		if(condition2)
			break;
		a();
		r();
	}
}

static void good_while4(void)
{
	a();
	while(1) {
		if(condition2) {
			r();
			break;
		}
	}
}

static void good_while5(void)
{
	a();
	while(1) {
		r();
		if(condition2)
			break;
		a();
	}
}

static void warn_while1(void)
{
	while(condition) {
		a();
	}
}

static void warn_while2(void)
{
	while(condition) {
		r();
	}
}

static void warn_while3(void)
{
	while(condition) {
		a();
		if(condition2)
			break;
		r();
	}
}

static void good_goto1(void)
{
    a();
    goto label;
label:
    r();
}

static void good_goto2(void)
{
    a();
    goto label;
    a();
    r();
label:
    r();
}

static void good_goto3(void)
{
    a();
    if(condition)
        goto label;
    a();
    r();
label:
    r();
}

static void good_goto4(void)
{
    if(condition)
        goto label;
    a();
    r();
label:
    ;
}

static void good_goto5(void)
{
    a();
    if(condition)
        goto label;
    r();
    return;
label:
    r();
}

static void warn_goto1(void)
{
    a();
    goto label;
    r();
label:
    ;
}

static void warn_goto2(void)
{
    a();
    goto label;
    r();
label:
    a();
    r();
}

static void warn_goto3(void)
{
    a();
    if(condition)
        goto label;
    r();
label:
    r();
}

static void warn_multiple1(void)
{
    a();
    a2();
}

static void warn_multiple2(void)
{
    a2();
    a();
}

static void warn_mixed1(void)
{
    a2();
    r();
}

static void warn_mixed2(void)
{
    a2();
    if (condition) {
        a();
        r2();
    }
    r();
}

static void warn_mixed3(void)
{
    a2();
    if (condition) {
        r2();
        return;
    }
    r();
}

static void warn_mixed4(void)
{
    a2();
    if (condition) {
        a();
        r();
        return;
    }
    r();
}

static void good_mixed1(void)
{
    if (condition) {
        a();
        r();
    } else {
        a2();
        r2();
    }
}

static void good_mixed2(void)
{
    if (condition) {
        a();
        r();
    }
    a2();
    r2();
}

static int need_lock(void) __attribute__((context(TEST,1,1)))
{
}

static void need_lock_exact(void) __attribute__((exact_context(TEST,1,1)))
{
}

static void need_lock2(void) __attribute__((context(TEST,1,1)))
{
    need_lock();
}

static void good_fn(void)
{
    a();
    need_lock();
    r();
}

static void good_fn2(void)
{
    a();
    a();
    need_lock();
    r();
    r();
}

static void good_fn2(void)
{
    a();
    if (condition)
        need_lock();
    r();
}

static void good_fn3(void) __attribute__((context(TEST,1,1)))
{
    if (condition)
        need_lock2();
}

static void warn_fn(void)
{
    a2();
    need_lock();
    r2();
}

static void warn_fn2(void)
{
    a2();
    need_lock2();
    r2();
}

static void good_exact_fn(void)
{
    a();
    need_lock_exact();
    r();
}

static void warn_exact_fn1(void)
{
    a();
    a();
    need_lock_exact();
    r();
    r();
}

static void warn_exact_fn2(void)
{
    a2();
    need_lock_exact();
    r2();
}

#define __acquire(x)	__context__(x,1)
#define __release(x)	__context__(x,-1)

#define rl() \
  do { __acquire(RCU); } while (0)

#define ru() \
  do { __release(RCU); } while (0)

static void good_mixed_with_if(void)
{
    rl();

    if (condition) {
        a();
        r();
    }

    ru();
}

/*
 * check-name: Check -Wcontext with lock names
 *
 * check-error-start
context-named.c:86:3: warning: context imbalance in 'warn_lock1' - wrong count at exit (TEST)
context-named.c:93:3: warning: context imbalance in 'warn_lock2' - wrong count at exit (TEST)
context-named.c:100:3: warning: context imbalance in 'warn_lock3' - wrong count at exit (TEST)
context-named.c:105:3: warning: context problem in 'warn_unlock1' - function 'r' expected different context
context-named.c:112:3: warning: context problem in 'warn_unlock2' - function 'r' expected different context
context-named.c:152:9: warning: context imbalance in 'warn_if1' - wrong count at exit (TEST)
context-named.c:162:9: warning: context imbalance in 'warn_if2' - wrong count at exit (TEST)
context-named.c:218:4: warning: context imbalance in 'warn_while1' - wrong count at exit (TEST)
context-named.c:225:4: warning: context problem in 'warn_while2' - function 'r' expected different context
context-named.c:235:4: warning: context imbalance in 'warn_while3' - wrong count at exit (TEST)
context-named.c:295:5: warning: context imbalance in 'warn_goto1' - wrong count at exit (TEST)
context-named.c:305:6: warning: context imbalance in 'warn_goto2' - wrong count at exit (TEST)
context-named.c:315:6: warning: context problem in 'warn_goto3' - function 'r' expected different context
context-named.c:321:7: warning: context imbalance in 'warn_multiple1' - wrong count at exit (TEST)
context-named.c:327:6: warning: context imbalance in 'warn_multiple2' - wrong count at exit (TEST2)
context-named.c:333:6: warning: context problem in 'warn_mixed1' - function 'r' expected different context
context-named.c:343:6: warning: context problem in 'warn_mixed2' - function 'r' expected different context
context-named.c:353:6: warning: context problem in 'warn_mixed3' - function 'r' expected different context
context-named.c:364:6: warning: context imbalance in 'warn_mixed4' - wrong count at exit (TEST2)
context-named.c:434:14: warning: context problem in 'warn_fn' - function 'need_lock' expected different context
context-named.c:441:15: warning: context problem in 'warn_fn2' - function 'need_lock2' expected different context
context-named.c:456:20: warning: context problem in 'warn_exact_fn1' - function 'need_lock_exact' expected different context
context-named.c:464:20: warning: context problem in 'warn_exact_fn2' - function 'need_lock_exact' expected different context
 * check-error-end
 */
