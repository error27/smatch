#define __cond_lock(c) ((c) ? ({ __context__(1); 1; }) : 0)

void a(void) __attribute__((context(0,1)))
{
	__context__(1);
}

void r(void) __attribute__((context(1,0)))
{
	__context__(-1);
}

extern void _ca(int fail);
#define ca(fail) __cond_lock(_ca(fail))

void good_paired1(void)
{
	a();
	r();
}

void good_paired2(void)
{
	a();
	r();
	a();
	r();
}

void good_paired3(void)
{
	a();
	a();
	r();
	r();
}

void good_lock1(void) __attribute__((context(0,1)))
{
	a();
}

void good_lock2(void) __attribute__((context(0,1)))
{
	a();
	r();
	a();
}

void good_lock3(void) __attribute__((context(0,1)))
{
	a();
	a();
	r();
}

void good_unlock1(void) __attribute__((context(1,0)))
{
	r();
}

void good_unlock2(void) __attribute__((context(1,0)))
{
	a();
	r();
	r();
}

void warn_lock1(void)
{
	a();
}

void warn_lock2(void)
{
	a();
	r();
	a();
}

void warn_lock3(void)
{
	a();
	a();
	r();
}

void warn_unlock1(void)
{
	r();
}

void warn_unlock2(void)
{
	a();
	r();
	r();
}

extern int condition, condition2;

int good_if1(void)
{
	a();
	if(condition) {
		r();
		return -1;
	}
	r();
	return 0;
}

void good_if2(void)
{
	if(condition) {
		a();
		r();
	}
}

void good_if3(void)
{
	a();
	if(condition) {
		a();
		r();
	}
	r();
}

int warn_if1(void)
{
	a();
	if(condition)
		return -1;
	r();
	return 0;
}

int warn_if2(void)
{
	a();
	if(condition) {
		r();
		return -1;
	}
	return 0;
}

void good_while1(void)
{
	a();
	while(condition)
		;
	r();
}

void good_while2(void)
{
	while(condition) {
		a();
		r();
	}
}

void good_while3(void)
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

void good_while4(void)
{
	a();
	while(1) {
		if(condition2) {
			r();
			break;
		}
	}
}

void good_while5(void)
{
	a();
	while(1) {
		r();
		if(condition2)
			break;
		a();
	}
}

void warn_while1(void)
{
	while(condition) {
		a();
	}
}

void warn_while2(void)
{
	while(condition) {
		r();
	}
}

void warn_while3(void)
{
	while(condition) {
		a();
		if(condition2)
			break;
		r();
	}
}

void good_goto1(void)
{
    a();
    goto label;
label:
    r();
}

void good_goto2(void)
{
    a();
    goto label;
    a();
    r();
label:
    r();
}

void good_goto3(void)
{
    a();
    if(condition)
        goto label;
    a();
    r();
label:
    r();
}

void good_goto4(void)
{
    if(condition)
        goto label;
    a();
    r();
label:
    ;
}

void good_goto5(void)
{
    a();
    if(condition)
        goto label;
    r();
    return;
label:
    r();
}

void warn_goto1(void)
{
    a();
    goto label;
    r();
label:
    ;
}

void warn_goto2(void)
{
    a();
    goto label;
    r();
label:
    a();
    r();
}

void warn_goto3(void)
{
    a();
    if(condition)
        goto label;
    r();
label:
    r();
}

void good_cond_lock1(void)
{
    if(ca(condition)) {
        condition2 = 1; /* do stuff */
        r();
    }
}

void warn_cond_lock1(void)
{
    if(ca(condition))
        condition2 = 1; /* do stuff */
    r();
}
