long foo(int a) { return a != 0; }

/*
 * check-name: setne0-sext
 * check-command: test-linearize -m64 -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-excludes: sext\\.
 */
