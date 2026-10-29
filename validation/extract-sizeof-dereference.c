/*
 * check-name: extract sizeof dereference
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-sizeof-dereference.c.extract.last
 * check-output-end
 */

struct descriptor {
	int value;
};

static unsigned long target(const struct descriptor *desc)
{
	return sizeof(*desc);
}
