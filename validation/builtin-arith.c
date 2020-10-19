

void test(void (*fun)(void));
void test(void (*fun)(void))
{
	typedef typeof(__builtin_trap) t;	// OK
	void (*f)(void);
	int i;

	f =  __builtin_trap;
	f = &__builtin_trap;
	f = *__builtin_trap;			// OK for GCC
	f =  __builtin_trap + 0;
	f =  __builtin_trap + 1;
	f =  __builtin_trap - 1;

	// (void) __builtin_trap;
	f = (void*) __builtin_trap;
	f = (unsigned long) __builtin_trap;

	i = !__builtin_trap;
	i = (__builtin_trap > fun);
	i = (__builtin_trap == fun);
	i = (fun <  __builtin_trap);
	i = (fun == __builtin_trap);

	__builtin_trap - fun;
	fun - __builtin_trap;
}

/*
 * check-name: builtin arithmetic
 * check-command: sparse -Wno-decl $file
 *
 * check-error-start
builtin-arith.c:10:14: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:11:13: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:12:14: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:13:14: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:13:29: error: arithmetics on pointers to functions
builtin-arith.c:14:14: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:14:29: error: arithmetics on pointers to functions
builtin-arith.c:15:14: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:15:29: error: arithmetics on pointers to functions
builtin-arith.c:18:21: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:19:29: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:21:14: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:22:14: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:23:14: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:24:21: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:25:21: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:27:9: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:27:24: error: subtraction of functions? Share your drugs
builtin-arith.c:28:15: error: taking the address of built-in function '__builtin_trap'
builtin-arith.c:28:13: error: subtraction of functions? Share your drugs
 * check-error-end
 */
