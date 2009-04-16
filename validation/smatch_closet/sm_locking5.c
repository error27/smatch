mutex_trylock(int name);
mutex_unlock(int name);

int a;
int func (void)
{
	int mylock = 1;

	if (a && !mutex_trylock(mylock)) {
		return;
	}
	return;  //<- my lock is not locked here.
}
