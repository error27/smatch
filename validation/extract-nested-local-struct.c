/*
 * check-name: extract local struct nested in another local struct
 * check-command: extract $file && gcc -std=gnu11 -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-nested-local-struct.c.extract
 * check-output-end
 */

int f(void *data)
{
	struct outer {
		int magic;
		struct inner {
			int size;
		} parts[4];
	} *label;
	struct inner *part;

	label = data;
	part = label->parts;
	return part->size;
}
