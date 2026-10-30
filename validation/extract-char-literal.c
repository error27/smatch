/*
 * check-name: extract character literal
 * check-command: extract $file && grep -Fq "case '6':" $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-char-literal.c.extract
 * check-output-end
 */

int classify_digit(int value)
{
	switch (value) {
	case '6':
		return 1;
	default:
		return 0;
	}
}
