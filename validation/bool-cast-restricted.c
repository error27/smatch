typedef unsigned   int __attribute__((bitwise)) large_t;
#define	LBIT	((__attribute__((force)) large_t) 1)

_Bool lfoo(large_t x) { return x; }
_Bool qfoo(large_t x) { _Bool r = x; return r; }
_Bool lbar(large_t x) { return ~x; }
_Bool qbar(large_t x) { _Bool r = ~x; return r; }
_Bool lbaz(large_t x) { return !x; }
_Bool qbaz(large_t x) { _Bool r = !x; return r; }
_Bool lqux(large_t x) { return x & LBIT; }
_Bool qqux(large_t x) { _Bool r = x & LBIT; return r; }


typedef unsigned short __attribute__((bitwise)) small_t;
#define	SBIT	((__attribute__((force)) small_t) 1)

_Bool sfoo(small_t x) { return x; }
_Bool tfoo(small_t x) { _Bool r = x; return r; }
_Bool sbar(small_t x) { return ~x; }
_Bool tbar(small_t x) { _Bool r = ~x; return r; }
_Bool sbaz(small_t x) { return !x; }
_Bool tbaz(small_t x) { _Bool r = !x; return r; }
_Bool squx(small_t x) { return x & SBIT; }
_Bool tqux(small_t x) { _Bool r = x & SBIT; return r; }

/*
 * check-name: bool-cast-restricted.c
 * check-command: sparse -Wno-decl $file
 *
 * check-error-start
bool-cast-restricted.c:19:32: warning: restricted small_t degrades to integer
bool-cast-restricted.c:20:35: warning: restricted small_t degrades to integer
 * check-error-end
 */
