_spin_trylock(int name);
_spin_lock(int name);
_spin_unlock(int name);

int func (void)
{
	int mylock = 1;

	if (!({frob(); frob(); _spin_trylock(mylock);})) 
		return;

	frob();
	_spin_unlock(mylock);

	if (((_spin_trylock(mylock)?1:0)?1:0))
		return;
	frob_somemore();
	_spin_unlock(mylock);

	return;
}
/*
 * check-name: Locking #3
 * check-command: smatch sm_locking3.c
 *
 * check-output-start
sm_locking3.c +18 func(13) error: double unlock 'mylock'
sm_locking3.c +20 func(15) warn: lock 'mylock' held on line 16 but not on 20.
 * check-output-end
 */
