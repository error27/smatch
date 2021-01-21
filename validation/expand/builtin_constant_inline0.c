static inline int is_const(long size)
{
	return __builtin_constant_p(size) ? size : 0;
}

int foo(void)
{
	return is_const(42);
}

/*
 * check-name: builtin_constant_inline0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
foo:
.L0:
	<entry-point>
	# call      %r1 <- is_const, $42
	ret.32      $42


 * check-output-end
 */
