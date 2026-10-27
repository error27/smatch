/*
 * check-name: extract signed enum initializer
 * check-command: extract $file && grep -Fq 'ERROR = -1,' $file.extract && grep -Fq 'signed char negative_char = ((signed char)(-1));' $file.extract && grep -Fq 'short negative_short = ((short)(-2));' $file.extract && grep -Fq 'long negative_long = ((long)(-3));' $file.extract && grep -Fq 'long long negative_long_long = ((long long)(-4));' $file.extract && grep -Fq 'unsigned char unsigned_char = ((unsigned char)0xff);' $file.extract && ! grep -Fq '0xffffffff' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-enum-negative-value.c.extract
 * check-output-end
 */

enum error {
	ERROR = -1,
};

static enum error error = ERROR;

signed char negative_char = (signed char)-1;
short negative_short = (short)-2;
long negative_long = (long)-3;
long long negative_long_long = (long long)-4;
unsigned char unsigned_char = (unsigned char)0xff;
