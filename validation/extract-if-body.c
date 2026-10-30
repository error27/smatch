/*
 * check-name: extract if body
 * check-command: extract $file && grep -Eq '^[[:space:]]+if [(]condition[)]$' $file.extract && grep -Eq '^[[:space:]][[:space:]]+return 1;$' $file.extract && grep -Eq '^[[:space:]]+else$' $file.extract && grep -Eq '^[[:space:]][[:space:]]+return 0;$' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-if-body.c.extract
 * check-output-end
 */

int test_if_body(int condition)
{
	if (condition)
		return 1;
	else
		return 0;
}
