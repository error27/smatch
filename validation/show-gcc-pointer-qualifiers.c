/*
 * check-name: show GCC pointer qualifiers
 * check-command: test-show-type --gcc $file
 *
 * check-output-start
int qualify(const char *source, struct lock_trace **const trace, struct lock_trace *restrict restricted, const char *const *nested, const char *const *const *triple);
 * check-output-end
 */

struct lock_trace;

int qualify(const char *source, struct lock_trace **const trace,
		   struct lock_trace *restrict restricted,
		   const char *const *nested, const char *const *const *triple)
{
	return 0;
}
