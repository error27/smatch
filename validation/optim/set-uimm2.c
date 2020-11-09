static _Bool setlt_umax(unsigned int a) { return (a <  ~0) == (a != ~0); }
static _Bool setle_umax(unsigned int a) { return (a <= ~1) == (a != ~0); }
static _Bool setge_umax(unsigned int a) { return (a >= ~0) == (a == ~0); }
static _Bool setgt_umax(unsigned int a) { return (a >  ~1) == (a == ~0); }

/*
 * check-name: set-uimm2
 * check-command: test-linearize $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
