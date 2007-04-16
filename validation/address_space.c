#define __user __attribute__((address_space(1)))

extern int poke_memory(void *addr);

static int sys_do_stuff(void __user *user_addr)
{
	return poke_memory(user_addr);
}
