struct run { long vcn; };

long test(int a, int b, int c, int d, struct run *rl)
{
	long r = 0, s, t;
	int i;

	r += a + b * c - d / 2 % 3;
	r += a - (b - c) - d;
	r += a && b || c && d;
	r += (a | b) & c;
	s = t = a + b;
	r += rl[i = 2].vcn;
	return r + s + t + i;
}

/*
 * check-name: extract parentheses from operator precedence
 * check-command: extract --last-func $file && grep -Fq ' - d / 2 % 3;' $file.extract.last && grep -Fq 'r += a - (b - c) - d;' $file.extract.last && grep -Fq 'r += (a && b) || (c && d);' $file.extract.last && grep -Fq 'r += (a | b) & c;' $file.extract.last && grep -Fq 's = t = a + b;' $file.extract.last && grep -Fq 'i = 2].vcn' $file.extract.last && gcc -std=gnu11 -Werror -Wparentheses -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-parentheses.c.extract.last
 * check-output-end
 */
