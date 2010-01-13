int __spin_trylock(int x);
int frob();
int i;
static int options_write(void)
{
        char buf[10];
	int lock;

	while(!__spin_trylock(lock))
		frob();
	__spin_unlock(lock);

	for (i = 0; i < 10 && frob(); i++)
		;
	buf[i] = '\0';
}
/*
 * check-name: smatch array check
 * check-command: smatch sm_array_overflow.c
 *
 * check-output-start
sm_array_overflow.c +15 options_write(11) error: buffer overflow 'buf' 10 <= 10
 * check-output-end
 */
