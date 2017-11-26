extern int fun(int a);

int symbol(int a)
{
	fun(a);
}

int pointer0(int a, int (*fun)(int))
{
	fun(a);
}

int pointer1(int a, int (*fun)(int))
{
	(*fun)(a);
}

int builtin(int a)
{
	__builtin_popcount(a);
}

/*
 * check-name: basic function calls
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-start
symbol:
.L0:
	<entry-point>
	call.32     %r2 <- fun, %arg1
	ret.32      %r2


pointer0:
.L2:
	<entry-point>
	call.32     %r5 <- %arg2, %arg1
	ret.32      %r5


pointer1:
.L4:
	<entry-point>
	call.32     %r8 <- %arg2, %arg1
	ret.32      %r8


builtin:
.L6:
	<entry-point>
	call.32     %r11 <- __builtin_popcount, %arg1
	ret.32      %r11


 * check-output-end
 */
