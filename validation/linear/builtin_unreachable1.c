void die(void);

int foo(int c)
{
	if (c)
		return 1;
	die();
	__builtin_unreachable();
}

/*
 * check-name: builtin_unreachable1
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
foo:
.L0:
	<entry-point>
	cbr         %arg1, .L1, .L2

.L1:
	ret.32      $1

.L2:
	call        die
	unreachable


 * check-output-end
 */
