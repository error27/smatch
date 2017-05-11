typedef unsigned   int __attribute__((bitwise)) large_t;
#define	LBIT	((__attribute__((force)) large_t) 1)

_Bool lfoo(large_t x) { return x; }
_Bool lbar(large_t x) { return ~x; }
_Bool lbaz(large_t x) { return !x; }
_Bool lqux(large_t x) { return x & LBIT; }


typedef unsigned short __attribute__((bitwise)) small_t;
#define	SBIT	((__attribute__((force)) small_t) 1)

_Bool sfoo(small_t x) { return x; }
_Bool sbar(small_t x) { return ~x; }
_Bool sbaz(small_t x) { return !x; }
_Bool squx(small_t x) { return x & SBIT; }

/*
 * check-name: bool-cast-restricted.c
 * check-command: sparse -Wno-decl $file
 *
 * check-error-start
bool-cast-restricted.c:14:32: warning: restricted small_t degrades to integer
 * check-error-end
 */
