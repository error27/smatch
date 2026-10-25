/*
 * check-name: extract anonymous pointer initializer
 * check-command: extract $file && gcc -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
 * extract: wrote $file.extract
 * check-output-end
 */

static int read_index(void *pointer)
{
	struct {
		unsigned long ifindex;
	} *ctx = pointer;

	return ctx->ifindex;
}
