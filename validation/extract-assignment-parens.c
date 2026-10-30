/*
 * check-name: extract assignment parentheses
 * check-command: extract $file && grep -Eq '^[[:space:]]+result = left = right;$' $file.extract && grep -Eq '^[[:space:]]+return [(]result = left[)];$' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-assignment-parens.c.extract
 * check-output-end
 */

int assign_values(int left, int right)
{
	int result;

	result = left = right;
	return (result = left);
}
