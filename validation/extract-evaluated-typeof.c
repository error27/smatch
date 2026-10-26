static const int value = 1;
static __typeof_unqual__(value) copy = 2;

/*
 * check-name: extract evaluated typeof
 * check-command: extract $file && grep -qx 'static __typeof_unqual__(value) copy.*' $file.extract
 *
 * check-output-start
extract: wrote extract-evaluated-typeof.c.extract
 * check-output-end
 */
