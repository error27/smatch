int zero_sub(int x) { return 0 - x; }

/*
 * check-name: simplify-zero-sub
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: neg\\..* %arg1
 */
