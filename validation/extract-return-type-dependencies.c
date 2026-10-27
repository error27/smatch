/*
 * check-name: extract function return type dependencies
 * check-command: extract $file && grep -qx 'struct direct;' $file.extract && grep -qx 'struct indirect;' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-return-type-dependencies.c.extract
 * check-output-end
 */

struct direct;
struct indirect;

static struct direct *get_direct(void);
static typeof((struct indirect *)0) get_indirect(void);
