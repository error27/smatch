int cte_sub_addl(int x) { return (1 - x) + 1; }

/*
 * check-name: simplify-cte-sub-addl
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: sub\\..*\\$2, %arg1
 */
