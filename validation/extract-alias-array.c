/*
 * check-name: extract alias array
 * check-command: extract $file && grep -Fc 'target[] __attribute__((used)) = {' $file.extract | grep -qx 1 && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-alias-array.c.extract
 * check-output-end
 */

static const int target[] = { 1 };

static typeof(target) target_alias
	__attribute__((used, alias("target")));

int use_target_alias(void)
{
	return target_alias[0];
}
