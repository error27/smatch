static int item;

#define FOUR(x) ((x) + (x) + (x) + (x))
#define LONG_TYPEOF(x) FOUR(FOUR(FOUR(FOUR(x))))

static typeof(LONG_TYPEOF(item)) *pointer;

/*
 * check-name: extract long typeof
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-long-typeof.c.extract
 * check-output-end
 */
