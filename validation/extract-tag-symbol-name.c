/*
 * check-name: extract tag and symbol with the same name
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-tag-symbol-name.c.extract.last
 * check-output-end
 */

struct shared {
	int value;
};

extern int shared;

struct shared *target(void)
{
	if (shared)
		return 0;
	return 0;
}
