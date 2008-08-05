#define __cond_lock(c) ((c) ? ({ __context__(1); 1; }) : 0)

static void a(void) __attribute__((context(0,1)))
{
	__context__(1);
}

static void r(void) __attribute__((context(1,0)))
{
	__context__(-1);
}

extern void _ca(int fail);
#define ca(fail) __cond_lock(_ca(fail))

static void good_paired1(void)
{
	a();
	r();
}

static void good_paired2(void)
{
	a();
	r();
	a();
	r();
}

static void good_paired3(void)
{
	a();
	a();
	r();
	r();
}

static void good_lock1(void) __attribute__((context(0,1)))
{
	a();
}

static void good_lock2(void) __attribute__((context(0,1)))
{
	a();
	r();
	a();
}

static void good_lock3(void) __attribute__((context(0,1)))
{
	a();
	a();
	r();
}

static void good_unlock1(void) __attribute__((context(1,0)))
{
	r();
}

static void good_unlock2(void) __attribute__((context(1,0)))
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

static void good_cond_lock1(void)
{
    if(ca(condition)) {
        condition2 = 1; /* do stuff */
        r();
    }
}

static void warn_cond_lock1(void)
{
    if(ca(condition))
        condition2 = 1; /* do stuff */
    r();
}

static void warn_odd_looping(void)
{
    int i;

    for (i = 0; i < 2; i++)
        a();
    for (i = 0; i < 2; i++)
        r();
}

static void warn_huge_switch(void)
{
    a();

    switch(condition) {
    case 1:
        r();
        break;
    case 2:
        r();
        break;
    case 3:
        r();
        break;
    case 4:
        r();
        break;
    case 5:
        r();
        break;
    case 11:
        r();
        break;
    case 12:
        r();
        break;
    case 13:
        r();
        break;
    case 14:
        r();
    case 15:
        r();
        break;
    case 16:
        r();
        break;
    case 17:
        r();
        break;
    }
}

static int warn_conditional(void)
{
    if (condition)
        return 0;

    a();
    if (condition == 0)
        return 1;
    r();
    return 0;
}

/*
 * check-name: Check -Wcontext
 *
 * check-error-start
context.c:71:3: warning: context imbalance in 'warn_lock1': wrong count at exit
context.c:71:3:    default context: wanted 0, got 1
context.c:78:3: warning: context imbalance in 'warn_lock2': wrong count at exit
context.c:78:3:    default context: wanted 0, got 1
context.c:85:3: warning: context imbalance in 'warn_lock3': wrong count at exit
context.c:85:3:    default context: wanted 0, got 1
context.c:90:3: warning: context problem in 'warn_unlock1': 'r' expected different context
context.c:90:3:    default context: wanted >= 1, got 0
context.c:97:3: warning: context problem in 'warn_unlock2': 'r' expected different context
context.c:97:3:    default context: wanted >= 1, got 0
context.c:137:9: warning: context imbalance in 'warn_if1': wrong count at exit
context.c:137:9:    default context: wanted 0, got 1
context.c:147:9: warning: context imbalance in 'warn_if2': wrong count at exit
context.c:147:9:    default context: wanted 0, got 1
context.c:203:4: warning: context imbalance in 'warn_while1': wrong count at exit
context.c:203:4:    default context: wanted 0, got 1
context.c:210:4: warning: context problem in 'warn_while2': 'r' expected different context
context.c:210:4:    default context: wanted >= 1, got 0
context.c:220:4: warning: context imbalance in 'warn_while3': wrong count at exit
context.c:220:4:    default context: wanted 0, got 1
context.c:280:5: warning: context imbalance in 'warn_goto1': wrong count at exit
context.c:280:5:    default context: wanted 0, got 1
context.c:290:6: warning: context imbalance in 'warn_goto2': wrong count at exit
context.c:290:6:    default context: wanted 0, got 1
context.c:300:6: warning: context problem in 'warn_goto3': 'r' expected different context
context.c:300:6:    default context: wanted >= 1, got 0
context.c:315:6: warning: context problem in 'warn_cond_lock1': 'r' expected different context
context.c:315:6:    default context: wanted >= 1, got 0
context.c:325:10: warning: context problem in 'warn_odd_looping': 'r' expected different context
context.c:325:10:    default context: wanted >= 1, got 0
context.c:360:10: warning: context problem in 'warn_huge_switch': 'r' expected different context
context.c:360:10:    default context: wanted >= 1, got 0
context.c:380:12: warning: context imbalance in 'warn_conditional': wrong count at exit
context.c:380:12:    default context: wanted 0, got 1
 * check-error-end
 */
