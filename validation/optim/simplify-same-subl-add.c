int subl_add(int x, int y) { return x - (x + y); }

/*
 * check-name: simplify-same-subl-add
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: neg\\..* %arg2
 * check-output-excludes: add\\.
 * check-output-excludes: sub\\.
 */
