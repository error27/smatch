/*
 * check-name: extract integer literal suffixes
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-literal-suffix.c.extract
 * check-output-end
 */

typedef typeof(__builtin_choose_expr(
	__builtin_types_compatible_p(typeof((const char *)0), typeof(0LL)) ||
	__builtin_types_compatible_p(typeof((const char *)0), typeof(0ULL)),
	0LL, 0L)) syscall_argument_t;

_Static_assert(__builtin_types_compatible_p(syscall_argument_t, long),
	"syscall argument must be long");
