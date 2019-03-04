static void kos(int *r, int a)
{
	r = ({ __builtin_types_compatible_p(int, int); });
}

/*
 * check-name: eval-bad-assign1
 *
 * check-error-start
eval-bad-assign1.c:3:11: warning: incorrect type in assignment (different base types)
eval-bad-assign1.c:3:11:    expected int *r
eval-bad-assign1.c:3:11:    got int
 * check-error-end
 */
