static _Bool setle_umax(unsigned int a) { return (a <= ~0) == 1; }
static _Bool setgt_umax(unsigned int a) { return (a >  ~0) == 0; }

/*
 * check-name: set-uimm1
 * check-command: test-linearize $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
