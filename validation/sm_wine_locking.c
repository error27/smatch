int create_window_handle(int x);
void WIN_ReleasePtr(int x);
void EnterCriticalSection(int x);
void LeaveCriticalSection(int x);
void USER_Lock(void);
void USER_Unlock(void);
int GDI_GetObjPtr(int x);
void GDI_ReleaseObj(int x);

void test1(void)
{
	int a;
	int b = create_window_handle(a);
	int c;
	int d, e;
	int z = frob();

	if (d = GDI_GetObjPtr(e))
		GDI_ReleaseObj(e);
	if (GDI_GetObjPtr(e))
		GDI_ReleaseObj(e);
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
 * check-command: smatch -p=wine --spammy sm_wine_locking.c
 *
 * check-output-start
sm_wine_locking.c +28 test1(18) error: double unlock 'create_window_handle:b'
sm_wine_locking.c +30 test1(20) warn: 'CriticalSection:c' is sometimes locked here and sometimes unlocked.
sm_wine_locking.c +33 test1(23) warn: inconsistent returns USER_Lock:: locked (30) unlocked (33)
 * check-output-end
 */
