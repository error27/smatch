# define __A	__attribute__((noderef))

struct x {
	int a;
	int b;
};

void h(void)
{
	char __A *p;
	char __A * * q1;
	char * __A * q2;
	struct x __A *xp;
	int __A *q;
	int __A *r;
	
	q1 = &p;
	q2 = &p;	/* This should complain */

	r = &*q;
	r = q;
	r = &*(q+1);	/* This should NOT complain */
	r = q+1;

	r = &xp->a;	/* This should NOT complain */
	r = &xp->b;
}
