static int e[] = { '\'', '\"', '\?', '\\',
                   '\a', '\b', '\f', '\n', '\r', '\t', '\v',
		   '\0', '\012', '\x7890', '\xabcd' };
static char *s = "\'\"\?\\ \a\b\f\n\r\t\v \377\xcafe";

static int bad_e[] = { '\c', '\0123', '\789', '\xdefg' };
/*
 * check-name: Character escape sequences
 *
 * check-error-start
escapes.c:6:26: warning: Unknown escape 'c'
escapes.c:3:34: warning: hex escape sequence out of range
escapes.c:3:44: warning: hex escape sequence out of range
escapes.c:4:18: warning: hex escape sequence out of range
escapes.c:6:30: warning: multi-character character constant
escapes.c:6:39: warning: multi-character character constant
escapes.c:6:47: warning: hex escape sequence out of range
escapes.c:6:47: warning: multi-character character constant
 * check-error-end
 */
