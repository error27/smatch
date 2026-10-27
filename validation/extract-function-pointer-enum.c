/*
 * check-name: extract function pointer enum argument
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-function-pointer-enum.c.extract
 * check-output-end
 */

enum mode {
	MODE_DISABLED,
	MODE_ENABLED,
};

typedef void (*callback_t)(enum mode mode);

struct state {
	callback_t callback;
};

static struct state state;
