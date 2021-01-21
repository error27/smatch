int subr_add(int x, int y) { return x - (y + x); }

/*
 * check-name: simplify-same-subr-add
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: neg\\..* %arg2
 * check-output-excludes: add\\.
 * check-output-excludes: sub\\.
 */
