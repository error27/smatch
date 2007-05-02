#define barrier() __asm__ __volatile__("": : :"memory")

static void f(void)
{
	barrier();
}
