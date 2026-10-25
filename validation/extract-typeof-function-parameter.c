/*
 * check-name: extract typeof function parameter
 * check-command: extract $file && gcc -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
 * extract: wrote $file.extract
 * check-output-end
 */

struct callback_argument {
	int value;
};

static struct callback_argument argument;

static void describe(typeof(argument) arg, char *text)
{
}

static void (*callback)(typeof(argument) arg, char *text) = describe;
