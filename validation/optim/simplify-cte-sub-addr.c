int cte_sub_addr(int x) { return 2 - (x + 1); }

/*
 * check-name: simplify-cte-sub-addr
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-contains: sub\\..*\\$1, %arg1
 */
