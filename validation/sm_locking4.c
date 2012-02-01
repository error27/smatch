void _spin_lock(int name);
void _spin_unlock(int name);

void frob(void){}
int a;
int b;
int func (void)
{
	int mylock = 1;
	int mylock2 = 2;

	if (1)
	      	_spin_unlock(mylock);
	frob();
	if (a)
		return;
	if (!0)
	      	_spin_lock(mylock);
	if (0)
	      	_spin_unlock(mylock);
	if (b)
		return;
	if (!1)
	      	_spin_lock(mylock);
	return 0;
}
/*
 * check-name: Smatch locking #4
 * check-command: smatch --project=kernel sm_locking4.c
 *
 * check-output-start
sm_locking4.c:25 func(18) warn: inconsistent returns spin_lock:mylock: locked (22,25) unlocked (16)
 * check-output-end
 */
