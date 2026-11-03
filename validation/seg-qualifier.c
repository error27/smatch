struct task_struct { int x; };

extern struct task_struct * const __seg_gs current_task;
extern __seg_fs int fs_var;
extern int __seg_gs *gs_ptr;
struct wq { int __seg_gs **cpu_pwq; int __seg_gs * __seg_gs *both; };
extern __seg_gs int toplevel;
__seg_gs int toplevel;

int f(struct wq *w);
int f(struct wq *w)
{
	int *plain = gs_ptr;

	return current_task->x + fs_var + *plain + **w->cpu_pwq +
	       (*(typeof(__seg_gs int) *)&toplevel);
}

/*
 * check-name: seg-qualifier
 *
 * check-error-start
seg-qualifier.c:13:22: warning: incorrect type in initializer (different address spaces)
seg-qualifier.c:13:22:    expected int *plain
seg-qualifier.c:13:22:    got int __seg_gs *extern [addressable] [toplevel] gs_ptr
 * check-error-end
 */
