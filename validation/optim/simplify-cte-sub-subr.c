int cte_sub_subr(int x) { return 1 - (1 - x); }

/*
 * check-name: simplify-cte-sub-subr
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..* %arg1
 */
