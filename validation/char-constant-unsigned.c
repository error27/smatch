int test(void) { return '\377' == 255; }

/*
 * check-name: char-constant-unsigned
 * check-command: test-linearize -Wno-decl -funsigned-char $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
