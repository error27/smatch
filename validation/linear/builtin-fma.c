double fma(double a, double x, double y)
{
	return __builtin_fma(a, x, y);
}

/*
 * check-name: builtin-fma
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
fma:
.L0:
	<entry-point>
	fmadd.64    %r4 <- %r1, %r2, %r3
	ret.64      %r4


 * check-output-end
 */
