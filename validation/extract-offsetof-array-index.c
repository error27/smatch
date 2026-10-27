/*
 * check-name: extract offsetof array index
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-offsetof-array-index.c.extract
 * check-output-end
 */

struct offset_array_index {
	int value;
	char name[16];
};

unsigned long offset_array_index(int index)
{
	return __builtin_offsetof(struct offset_array_index, name[index + 2]);
}
