int add_subl(int x, int y) { return (x + y) - x; }

/*
 * check-name: simplify-same-add-subl
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
add_subl:
.L0:
	<entry-point>
	ret.32      %arg2


 * check-output-end
 */
