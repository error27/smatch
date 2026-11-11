struct ctr {
	unsigned long count;
};

struct srcu {
	__seg_gs struct ctr *ctrp;
	struct ctr __seg_gs *__seg_fs *pp;
};

extern unsigned long __seg_gs cpu_offset;
extern __seg_gs struct ctr elems[2];
extern struct srcu srcu;

int f(void)
{
	unsigned long __seg_gs *p = (unsigned long __seg_gs *)(unsigned long)&cpu_offset;
	__typeof_unqual__(cpu_offset) copy = *p;

	return srcu.ctrp->count + (*srcu.pp)->count + elems[1].count + copy;
}

/*
 * --cerberus drops the x86 named address spaces.
 *
 * check-name: extract cerberus address space
 * check-command: extract --cerberus $file && gcc -Werror -fsyntax-only $file && gcc -Werror -x c -fsyntax-only $file.extract && grep -c __seg_ $file.extract
 *
 * check-output-start
extract: wrote extract-cerberus-address-space.c.extract
0
 * check-output-end
 * check-exit-value: 1
 */
