/*
 * check-name: extract pointer to typeof of an incomplete struct
 * check-command: extract $file >$file.output 2>&1 && ! grep -Fq 'missing definition for struct rq' $file.output && cat $file.output && gcc -std=gnu11 -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-typeof-pointer-incomplete.c.extract
 * check-output-end
 */

struct rq;
extern struct rq runqueues;

struct s {
	int a;
};
extern struct s v;

void *this_rq(unsigned long offset)
{
	return (__typeof_unqual__(*(&runqueues)) *)(offset + (unsigned long)&runqueues);
}

int copy_v(void)
{
	typeof(v) copy = v;

	return copy.a;
}
