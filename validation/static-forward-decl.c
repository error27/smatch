int fref(void);
int fref(void) { return 0; }

static
int floc(void);
int floc(void) { return 0; }

static
int oloc;
int oloc = 0;

/*
 * check-name: static forward declaration
 */
