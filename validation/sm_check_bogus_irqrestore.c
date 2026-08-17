void spin_unlock_irqrestore(void *lock, unsigned long flags);

void test(void *lock, unsigned long flags)
{
	spin_unlock_irqrestore(lock, 0);
	spin_unlock_irqrestore(lock, flags);
}

/*
 * check-name: smatch: check bogus irqrestore flags
 * check-command: smatch --no-data -p=kernel sm_check_bogus_irqrestore.c
 *
 * check-output-start
sm_check_bogus_irqrestore.c:5 test() error: calling 'spin_unlock_irqrestore()' with bogus flags
 * check-output-end
 */
