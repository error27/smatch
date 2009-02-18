_spin_lock(int name);
_spin_unlock(int name);

int a;
int b;
int func (void)
{
	int mylock = 1;
	int mylock2 = 1;
	int mylock3 = 1;

	if (a) {
		return;
	}

	_spin_lock(mylock);
	_spin_unlock(mylock);

	if (b) {
		_spin_unlock(mylock2);
		return;
	}
	
	if (a)
		_spin_lock(mylock3);
	return;
}
/*
 * check-name: Locking inconsistencies
 * check-command: smatch sm_locking.c
 *
 * check-output-start
sm_locking.c +26 func(20) Unclear if 'mylock3' is locked or unlocked.
sm_locking.c +26 func(20) Lock 'mylock2' held on line 26 but not on 21.
 * check-output-end
 */
