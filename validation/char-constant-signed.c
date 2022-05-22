int test(void) { return '\377' == -1; }

/*
 * check-name: char-constant-signed
 * check-command: test-linearize -Wno-decl -fsigned-char $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
