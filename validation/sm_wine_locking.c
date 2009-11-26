int create_window_handle(int x);
void WIN_ReleasePtr(int x);
void EnterCriticalSection(int x);
void LeaveCriticalSection(int x);
void USER_Lock(void);
void USER_Unlock(void);

void test1(void)
{
	int a;
	int b = create_window_handle(a);
	int c;
	int z = frob();

	EnterCriticalSection(c);
	USER_Lock();


	if (b) {
		LeaveCriticalSection(c);
		WIN_ReleasePtr(b);
	}
	WIN_ReleasePtr(b);
	if (z)
		return;
	USER_Unlock();
	if (!b)
		LeaveCriticalSection(c);
}
/*
 * check-name: WINE locking
 * check-command: smatch -p=wine sm_wine_locking.c
 *
 * check-output-start
sm_wine_locking.c +23 test1(15) error: double unlock 'create_window_handle:b'
sm_wine_locking.c +25 test1(17) warn: 'CriticalSection:c' is sometimes locked here and sometimes unlocked.
sm_wine_locking.c +28 test1(20) warn: inconsistent returns USER_Lock:: locked (25) unlocked (28)
 * check-output-end
 */
