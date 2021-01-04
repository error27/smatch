_Bool lt_p(int a) { return (a >  0) == (a >=  1); }
_Bool ge_p(int a) { return (a <= 0) == (a <   1); }

_Bool lt_m(int a) { return (a <  0) == (a <= -1); }
_Bool ge_m(int a) { return (a >= 0) == (a >  -1); }

_Bool lt_x(int a) { return (a <= 1234) == (a < 1235); }
_Bool ge_x(int a) { return (a >= 1234) == (a > 1233); }

/*
 * check-name: canonical-cmps
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
