# define __seg_gs		__attribute__((address_space(__seg_gs)))
static int __seg_gs m;

static int __seg_gs bad_manual (void)
{
	return (*(int *)&m);
}

static int __seg_gs good_manual (void)
{
	return (*(int __seg_gs *)&m);
}

static int bad_typeof (void)
{
	return (*(typeof_unqual(m) *)&m);
}

static int __seg_gs good_typeof (void)
{
	return (*(volatile typeof(m) *)&m);
}

/*
 * check-name: typeof address space
 * check-command: ./sparse typeof-as.c
 *
 * check-error-start
typeof-as.c:6:19: warning: cast removes address space '__seg_gs' of expression
typeof-as.c:16:19: warning: cast removes address space '__seg_gs' of expression
 * check-error-end
 */
