/*
 * check-name: extract pointer compound assignment
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-pointer-compound-assignment.c.extract.last
 * check-output-end
 */

struct seq { int reg; int def; };

const struct seq *skip(const struct seq *p, int n, int back)
{
	p += n;
	p -= back;
	return p;
}
