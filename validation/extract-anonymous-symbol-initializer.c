/*
 * check-name: extract anonymous symbol initializer
 * check-command: extract $file && gcc -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
 * extract: wrote $file.extract
 * check-output-end
 */

static void report(const char *file, int line, const char *message)
{
}

static void test(void)
{
	report(__FILE__, __LINE__, "message");
}
