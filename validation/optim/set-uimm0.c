static _Bool setlt0(unsigned int a) { return (a <   0u) == 0; }
static _Bool setge0(unsigned int a) { return (a >=  0u) == 1; }
static _Bool setle0(unsigned int a) { return (a <=  0u) == (a == 0); }
static _Bool setgt0(unsigned int a) { return (a >   0u) == (a != 0); }
static _Bool setlt1(unsigned int a) { return (a <   1u) == (a == 0); }
static _Bool setge1(unsigned int a) { return (a >=  1u) == (a != 0); }

/*
 * check-name: set-uimm0
 * check-command: test-linearize $file
 *
 * check-output-ignore
 * check-output-pattern(6): ret\\.1 *\\$1
 */
