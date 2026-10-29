/*
 * check-name: extract function pointer parameter forward declaration
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-function-pointer-parameter-forward.c.extract
 * check-output-end
 */

struct operations {
	void (*callback)(struct dependency *);
};

struct dependency {
	int value;
};

static struct operations operations;
