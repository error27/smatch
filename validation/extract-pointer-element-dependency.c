/*
 * check-name: extract incomplete pointer element dependency
 * check-command: extract --last-func $file >$file.output 2>&1 && ! grep -Fq 'missing definition for struct hidden' $file.output && cat $file.output && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-pointer-element-dependency.c.extract.last
 * check-output-end
 */

struct hidden;

extern struct hidden *entries[2][2];

struct hidden *(*target(void))[2]
{
	return entries + 1;
}
