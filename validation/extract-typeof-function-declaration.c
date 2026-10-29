/*
 * check-name: extract typeof function declaration
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-typeof-function-declaration.c.extract
 * check-output-end
 */

void source_function(void)
{
}

extern typeof(source_function) declared_function;

static void target(void)
{
	declared_function();
}
