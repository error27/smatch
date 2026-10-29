static int data;

/*
 * check-name: extract no last function
 * check-command: extract --last-func $file >$file.output 2>&1 && test -f $file.extract.last && test ! -s $file.extract.last && cat $file.output && gcc -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
warning: no function was found in extract-no-last-function.c
extract: wrote extract-no-last-function.c.extract.last
 * check-output-end
 */
