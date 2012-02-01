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
 * check-name: Smatch locking #1
 * check-command: smatch --project=kernel --spammy sm_locking.c
 *
 * check-output-start
sm_locking.c:26 func(20) warn: 'spin_lock:mylock3' is sometimes locked here and sometimes unlocked.
sm_locking.c:26 func(20) warn: inconsistent returns spin_lock:mylock2: locked (13,26) unlocked (21)
 * check-output-end
 */
