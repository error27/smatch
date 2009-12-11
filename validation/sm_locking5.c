int mutex_lock_interruptible(int x);
void mutex_unlock(int x);

void frob(void) {
	int lock;

        if (mutex_lock_interruptible(lock) < 0)
                return;
	return;
}
/*
 * check-name: Smatch locking #5
 * check-command: smatch -p=kernel sm_locking5.c
 *
 * check-output-start
sm_locking5.c +9 frob(5) warn: inconsistent returns mutex:lock: locked (9) unlocked (8)
 * check-output-end
 */
