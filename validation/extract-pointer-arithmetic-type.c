/*
 * check-name: extract aggregate used by pointer arithmetic
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-pointer-arithmetic-type.c.extract.last
 * check-output-end
 */

struct page {
	int value;
};

extern struct page *get_page(void);

struct page *target(void)
{
	return get_page() + 1;
}
