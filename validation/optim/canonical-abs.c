_Bool abs0(int a) { return (a < 0 ? -a : a) == (a >= 0 ? a : -a); }
_Bool abs1(int a) { return (a < 0 ? -a : a) == (a >  0 ? a : -a); }
_Bool abs2(int a) { return (a < 0 ? -a : a) == (a <= 0 ? -a : a); }

/*
 * check-name: canonical-abs1
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
