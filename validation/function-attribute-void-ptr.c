#define __noreturn __attribute__((__noreturn__))

void fun(void *);
void __noreturn die(void);

static void foo(void)
{
	void *ptr = die;
	fun(die);
}

/*
 * check-name: function-attribute-void-ptr
 */
