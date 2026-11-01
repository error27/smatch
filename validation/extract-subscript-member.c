struct run { long vcn; };

long *test(struct run *rl, int i, int n)
{
	long vcn;

	vcn = rl[i].vcn + (rl + n)->vcn;
	vcn += rl[i = n].vcn;
	return &rl[vcn].vcn;
}

/*
 * check-name: extract member access through a subscript
 * check-command: extract --last-func $file && grep -Fq 'rl[i].vcn) + (rl[n].vcn' $file.extract.last && grep -Fq 'return &rl[vcn].vcn;' $file.extract.last && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-subscript-member.c.extract.last
 * check-output-end
 */
