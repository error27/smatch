/*
 * check-name: show GCC pointer qualifiers
 * check-command: test-show-type --gcc $file
 *
 * check-output-start
int qualify(const char *source, struct lock_trace **const trace, struct lock_trace *restrict restricted);
 * check-output-end
 */

struct lock_trace;

int qualify(const char *source, struct lock_trace **const trace,
		   struct lock_trace *restrict restricted)
{
	return 0;
}
