struct task_struct { int x; };
struct s { int x; };

extern struct task_struct * const __seg_gs current_task;
extern __seg_fs int fs_var;
extern int __seg_gs *gs_ptr;
struct wq { int __seg_gs **cpu_pwq; int __seg_gs * __seg_gs *both; };
static __seg_gs __typeof__(struct s [2]) percpu_array;
extern const __seg_gs int const_array[3];

int f(struct wq *w);
int f(struct wq *w)
{
	return current_task->x + fs_var + *gs_ptr + **w->cpu_pwq +
	       **w->both + percpu_array[1].x + const_array[0];
}

/*
 * check-name: extract-seg-qualifier
 * check-command: extract --func=f $file && gcc -std=gnu11 -Wall -Werror -x c -c -o /dev/null $file.extract.f && grep -e __seg_ $file.extract.f
 *
 * check-output-start
extract: wrote extract-seg-qualifier.c.extract.f
	__seg_gs int **cpu_pwq;
	__seg_gs int *__seg_gs *both;
extern struct task_struct *const __seg_gs current_task;
extern __seg_fs int fs_var;
extern __seg_gs int *gs_ptr;
static __seg_gs __typeof__(struct s [2]) percpu_array;
extern const __seg_gs __typeof__(int [3]) const_array;
 * check-output-end
 */
