extern int fun(void);

int ff(void) { return fun(); }

int f0(void) { return (fun)(); }

/*
 * check-name: direct calls
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: load
 * check-output-pattern(2): call\..* fun
 */
