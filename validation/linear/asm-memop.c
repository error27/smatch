static int foo(int *p)
{
	asm("op %0" : "=m" (p[0]));

	return p[0];
}

/*
 * check-name: linear-asm-memop
 * check-command: test-linearize $file
 *
 * check-output-start
foo:
.L0:
	<entry-point>
	asm         "op %0"
		out: "=m" (%arg1)
	load.32     %r4 <- 0[%arg1]
	ret.32      %r4


 * check-output-end
 */
