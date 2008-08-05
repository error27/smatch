static unsigned int ok1 = !1 && 2;
static unsigned int bad1 = !1 & 2;
/*
 * check-name: Dubious bitwise operation on !x
 *
 * check-error-start
dubious-bitwise-with-not.c:2:31: warning: dubious: !x & y
 * check-error-end
 */
