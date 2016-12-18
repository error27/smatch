int neg_add(int x, int y) { return -x + y; }

/*
 * check-name: simplify-neg-add
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: sub\\..*%arg2, %arg1
 */
