/*
 * check-name: extract same-named local tags in different functions
 * check-command: extract $file && gcc -std=gnu11 -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-local-tag-same-name.c.extract
 * check-output-end
 */

struct inner { char c; };
struct inner global_inner;

int f(void *data)
{
	struct outer {
		struct inner {
			int size;
		} parts[4];
	} *label = data;
	struct inner *part = label->parts;

	return part->size + global_inner.c;
}

int g(void)
{
	struct inner { long x; } a, b;

	a = b;
	return a.x;
}

int h(void *data)
{
	struct outer {
		int magic;
		struct inner {
			long size;
		} parts[4];
	} *label = data;
	struct inner *part = label->parts;

	return part->size;
}
