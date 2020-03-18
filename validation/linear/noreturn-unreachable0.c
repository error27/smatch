extern void die(void) __attribute__((noreturn));

int foo(void)
{
	die();
	return 0;
}

/*
 * check-name: noreturn-unreachable0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
foo:
.L0:
	<entry-point>
	call        die
	unreachable


 * check-output-end
 */
