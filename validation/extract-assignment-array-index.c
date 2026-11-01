struct run { long vcn; };

long test(struct run *rl, int n)
{
	int marker;
	long vcn;

	vcn = rl[marker = n].vcn;
	return vcn + marker;
}

/*
 * check-name: extract assignment in an array index
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-assignment-array-index.c.extract.last
 * check-output-end
 */
