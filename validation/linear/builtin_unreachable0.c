int foo(int p)
{
	if (p == 3)
		__builtin_unreachable();
	return p;
}

/*
 * check-name: builtin_unreachable0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
foo:
.L0:
	<entry-point>
	seteq.32    %r2 <- %arg1, $3
	cbr         %r2, .L1, .L2

.L1:
	unreachable

.L2:
	ret.32      %arg1


 * check-output-end
 */
