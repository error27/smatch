/*
 * This makes us really hurl chunks, causing
 * infinite recursion until we run out of stack.
 *
 * It _should_ result in just a single plain
 *
 *	"foo"
 *
 * (without the quotes).
 */
#define func(x) x
#define bar func(
#define foo bar foo
foo )
