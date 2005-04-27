/*
 * More examples from the comp.std.c discussion.
 *
 * This should result in bar(bar). We get it right.
 */
#define foo bar
#define mac(x) x(foo)

mac(foo)

