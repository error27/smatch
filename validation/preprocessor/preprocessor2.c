/*
 * This one we happen to get right.
 *
 * It should result in a simple
 *
 *	a + b
 *
 * for a proper preprocessor.
 */
#define TWO a, b

#define UNARY(x) BINARY(x)
#define BINARY(x, y) x + y

UNARY(TWO)
