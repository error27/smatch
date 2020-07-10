#define __bitwise __attribute__((bitwise))

typedef unsigned int __bitwise t;

unsigned int fun(void);

static t (*ptr)(void) = fun;

/*
 * check-name: bitwise-function-pointer
 *
 * check-error-start
bitwise-function-pointer.c:7:25: warning: incorrect type in initializer (different base types)
bitwise-function-pointer.c:7:25:    expected restricted t ( *static [toplevel] ptr )( ... )
bitwise-function-pointer.c:7:25:    got unsigned int ( * )( ... )
 * check-error-end
 */
