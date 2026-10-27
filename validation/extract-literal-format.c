/*
 * check-name: extract literal format
 * check-command: extract $file && grep -Fqx 'unsigned int explicit_unsigned = 0x1;' $file.extract && grep -Fqx 'int plain = 17;' $file.extract && grep -Fqx 'int power = 0x20;' $file.extract && grep -Fqx 'int mask = 0x70;' $file.extract && grep -Fq 'value & 0x10' $file.extract && grep -Fq 'value != 0x3' $file.extract && grep -Fq 'return (-4000);' $file.extract && ! grep -Fq '((int)' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-literal-format.c.extract
 * check-output-end
 */

unsigned int explicit_unsigned = 1U;
int plain = 17;
int power = 32;
int mask = 0x70;

int format_literals(int value)
{
	if (value & 16)
		return 1;
	if (value != 3)
		return -4000;
	return 18;
}
