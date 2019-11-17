#define __noreturn __attribute__((__noreturn__))

void __noreturn fun(void);

_Static_assert([void (__noreturn *)(void)] == [typeof(&fun)], "");

/*
 * check-name: function-attribute-inner
 */
