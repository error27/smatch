# define __A	__attribute__((noderef))
void h(void)
{
	char __A *p;
	char __A * * q1;
	char * __A * q2;
	q1 = &p;
	q2 = &p;
}
