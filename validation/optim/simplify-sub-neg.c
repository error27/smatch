int sub_neg(int x, int y) { return x - -y; }

/*
 * check-name: simplify-sub-neg
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-contains: add\\..*%arg., %arg.
 */
