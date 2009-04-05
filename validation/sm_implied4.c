void _spin_lock(int name);
void _spin_unlock(int name);

void frob(void){}
int a;
int b;
int c;
int func (void)
{
	int mylock = 1;
	int mylock2 = 2;

	if (!a)
	      	_spin_lock(mylock);
	if (b)
		frob();
	if (!a)
	      	_spin_unlock(mylock);
	if (a)
	      	_spin_lock(mylock);
	if (c)
		return 0;
	if (a)
	      	_spin_unlock(mylock);
	return 0;
}

/*
 * check-name: Smatch implied #4
 * check-command: smatch sm_implied4.c
 *
 * check-output-start
sm_implied4.c +22 func(14) warn: 'mylock' is sometimes locked here and sometimes unlocked.
 * check-output-end
 */
