/*
 * check-name: extract function pointer return forward declaration
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-function-pointer-return-forward.c.extract
 * check-output-end
 */

struct outer;
struct dependency;

struct outer {
	struct dependency dependency;
};

struct dependency {
	struct outer *(*get_outer)(void);
};

static struct outer outer;
