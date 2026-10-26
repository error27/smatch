static const int value = 1;
static __typeof_unqual__(value) copy = 2;

/*
 * check-name: extract typeof unqual
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-typeof-unqual.c.extract
 * check-output-end
 */
