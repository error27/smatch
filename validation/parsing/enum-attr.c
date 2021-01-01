#define __attr __attribute__((deprecated))

enum {
	old __attr,
	cur __attr = 42,
	new,
};

enum odd {
	odd = __attr 33,
};

enum bad {
	bad = 43 __attr,
};

/*
 * check-name: enum-attr
 *
 * check-error-start
parsing/enum-attr.c:10:15: error: typename in expression
parsing/enum-attr.c:10:15: error: undefined identifier '__attribute__'
parsing/enum-attr.c:10:15: error: bad constant expression type
parsing/enum-attr.c:10:22: error: Expected } at end of specifier
parsing/enum-attr.c:10:22: error: got 33
parsing/enum-attr.c:14:18: error: Expected } at end of specifier
parsing/enum-attr.c:14:18: error: got __attribute__
 * check-error-end
 */
