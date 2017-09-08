int  or_not0(int a) { return a | ~0; }
int and_not0(int a) { return a & ~0; }

/*
 * check-name: bool-not-zero
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
or_not0:
.L0:
	<entry-point>
	ret.32      $0xffffffff


and_not0:
.L2:
	<entry-point>
	ret.32      %arg1


 * check-output-end
 */
