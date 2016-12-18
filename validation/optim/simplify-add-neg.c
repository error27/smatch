int add_neg(int x, int y) { return x + -y; }

/*
 * check-name: simplify-add-neg
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: sub\\..*%arg1, %arg2
 */
