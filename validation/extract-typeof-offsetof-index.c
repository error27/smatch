/*
 * check-name: extract offsetof index inside typeof
 * check-command: extract $file && gcc -std=gnu11 -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-typeof-offsetof-index.c.extract
 * check-output-end
 */

struct fp_list { short count; short max; int fp[]; };
extern void *dup(const void *p, unsigned long size);

struct fp_list *fp_dup(struct fp_list *fpl)
{
	return ({ typeof(dup(fpl, __builtin_offsetof(struct fp_list, fp[fpl->count]))) _res = dup(fpl, __builtin_offsetof(struct fp_list, fp[fpl->count])); _res; });
}
