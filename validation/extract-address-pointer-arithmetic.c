/*
 * check-name: extract pointer arithmetic on an address-of expression
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-address-pointer-arithmetic.c.extract.last
 * check-output-end
 */

struct cache_stats {
	unsigned int lookups;
};

extern struct cache_stats stats;

struct cache_stats *get_stats(unsigned long offset)
{
	const void *verify = (typeof(&stats + 0))0;

	(void)verify;
	return &stats + offset;
}
