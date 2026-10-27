/*
 * check-name: extract signed enum initializer
 * check-command: extract $file && grep -Fq 'ERROR = -1,' $file.extract && ! grep -Fq '0xffffffff' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-enum-negative-value.c.extract
 * check-output-end
 */

enum error {
	ERROR = -1,
};

static enum error error = ERROR;
