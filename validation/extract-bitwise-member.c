/*
 * check-name: extract __bitwise applied directly to a member
 * check-command: extract $file && grep -Fq 'int *restrict rp;' $file.extract && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-bitwise-member.c.extract
 * check-output-end
 */

typedef int s32;

struct msg {
	s32 __attribute__((bitwise)) oppidx;
	s32 *__attribute__((bitwise)) ptr;
	int *restrict rp;
};

int get(struct msg *m)
{
	return (int)m->oppidx;
}
