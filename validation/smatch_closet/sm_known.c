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

	if (1)
	      	_spin_unlock(mylock);
	frob();
	if (1)
	      	_spin_lock(mylock);
	if (0)
	      	_spin_unlock(mylock);
	frob();
	if (0)
	      	_spin_lock(mylock);
	return 0;
}

/*
 * smatch currently has --known-conditions off by default so it 
 * trips up over this check.  Smatch extra needs to be improved, smatch needs
 * to do a 2 pass check, and then known conditions can be enabled by default.
 */
  
