/*
 * check-name: extract pointer increment
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-pointer-increment.c.extract.last
 * check-output-end
 */

struct element {
	int value;
};

static int target(struct element *element)
{
	element++;
	return element->value;
}
