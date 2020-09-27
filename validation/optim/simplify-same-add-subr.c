int add_subr(int x, int y) { return (x + y) - y; }

/*
 * check-name: simplify-same-add-subr
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
add_subr:
.L0:
	<entry-point>
	ret.32      %arg1


 * check-output-end
 */
