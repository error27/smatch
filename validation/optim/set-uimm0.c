static _Bool setlt0(unsigned int a) { return (a <   0u) == 0; }
static _Bool setge0(unsigned int a) { return (a >=  0u) == 1; }

/*
 * check-name: set-uimm0
 * check-command: test-linearize $file
 *
 * check-output-ignore
 * check-output-pattern(2): ret\\.1 *\\$1
 */
