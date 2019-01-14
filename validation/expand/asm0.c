static void foo(void)
{
	asm("" :: "i" (42 & 3));
	asm("" :: "i" (__builtin_constant_p(0)));
}

/*
 * check-name: expand-asm0
 * check-command: test-linearize $file
 *
 * check-output-start
foo:
.L0:
	<entry-point>
	asm         ""
		in: "i" ($2)
	asm         ""
		in: "i" ($1)
	ret


 * check-output-end
 */
