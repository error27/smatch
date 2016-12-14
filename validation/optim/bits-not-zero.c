int  or_not0(int a) { return a | ~0; }

/*
 * check-name: bool-not-zero
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
or_not0:
.L0:
	<entry-point>
	ret.32      $0xffffffff


 * check-output-end
 */
