int sub_cte(int x) { return (x - 1) != (x + -1); }

/*
 * check-name: canonical-sub-cte
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-contains: ret\\..*\\$0
 */
