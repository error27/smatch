# define __A	__attribute__((noderef))

struct x {
	int a;
	int b;
};

struct y {
	int a[2];
};

void h(void)
{
	char __A *p;
	char __A * * q1;
	char * __A * q2;
	struct x __A *xp;
	struct x __A x;
	int __A *q;
	int __A *r;
	struct y __A *py;
	
	q1 = &p;
	q2 = &p;	/* This should complain */

	r = &*q;
	r = q;
	r = &*(q+1);	/* This should NOT complain */
	r = q+1;

	r = &xp->a;	/* This should NOT complain */
	r = &xp->b;
	r = &(*xp).a;
	r = &(*xp).b;

	r = &x.a;
	r = &x.b;

	r = py->a;
	r = py->a+1;
	r = &py->a[0];
}
