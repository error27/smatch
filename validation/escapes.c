static int e[] = { '\'', '\"', '\?', '\\',
                   '\a', '\b', '\f', '\n', '\r', '\t', '\v',
		   '\0', '\012', '\x7890', '\xabcd' };
static char *s = "\'\"\?\\ \a\b\f\n\r\t\v \377\xcafe";

static int bad_e[] = { '\c', '\0123', '\789', '\xdefg' };
/*
 * check-name: Character escape sequences
 *
 * check-error-start
escapes.c:6:27: warning: Unknown escape 'c'
escapes.c:6:35: error: Bad character constant
escapes.c:6:38: error: Bad character constant
escapes.c:6:42: error: Bad character constant
escapes.c:6:46: error: Bad character constant
escapes.c:6:53: error: Bad character constant
escapes.c:6:56: error: Bad character constant
escapes.c:6:42: error: Expected } at end of initializer
escapes.c:6:42: error: got 89
 * check-error-end
 */
