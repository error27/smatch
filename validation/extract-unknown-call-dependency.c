/*
 * check-name: extract unknown call dependency
 * check-command: extract --last-func $file && gcc -Wno-implicit-function-declaration -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-unknown-call-dependency.c.extract.last
 * check-output-end
 */

int target(void)
{
	return unknown_function();
}
