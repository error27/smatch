static inline int fun(void) { return 42; }

int fi(void) { return fun(); }

int i0(void) { return (fun)(); }

/*
 * check-name: inline calls
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: load
 * check-output-excludes: call
 * check-output-pattern(2): ret\..* \\$42
 */
