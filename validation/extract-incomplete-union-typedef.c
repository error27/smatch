/*
 * check-name: extract incomplete union typedef
 * check-command: extract $file 2> $file.error && test ! -s $file.error && grep -Fqx 'union opaque;' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-incomplete-union-typedef.c.extract
 * check-output-end
 */

typedef union opaque opaque_t;

struct holder {
	opaque_t *pointer;
};

static struct holder holder;
