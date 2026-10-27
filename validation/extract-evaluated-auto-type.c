/*
 * check-name: extract evaluated auto type
 * check-command: extract $file && grep -Fq '__auto_type copy = value;' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-evaluated-auto-type.c.extract
 * check-output-end
 */

int type_size(int *value)
{
	__auto_type copy = value;

	return sizeof(copy);
}
