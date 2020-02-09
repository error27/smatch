static inline int add(int a, int b)
{
	return a + b;
}

int foo0(int x, int y)
{
	return add(x, y);
}

int foo1(int x)
{
	return add(x, 1);
}

int foo2(void)
{
	return add(1, 2);
}

/*
 * check-name: inline_base0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
foo0:
.L0:
	<entry-point>
	add.32      %r5 <- %arg1, %arg2
	ret.32      %r5


foo1:
.L3:
	<entry-point>
	add.32      %r10 <- %arg1, $1
	ret.32      %r10


foo2:
.L6:
	<entry-point>
	ret.32      $3


 * check-output-end
 */
