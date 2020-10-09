static int ok_int(int *ptr, int val)
{
	return __sync_add_and_fetch(ptr, val);
}

static long* ok_ptr(long **ptr, long *val)
{
	return __sync_add_and_fetch(ptr, val);
}

static void chk_ret_ok(long *ptr, long val)
{
	_Static_assert([typeof(__sync_add_and_fetch(ptr, val))] == [long], "");
}

static int chk_val(int *ptr, long val)
{
	// OK: val is converted to an int
	return __sync_add_and_fetch(ptr, val);
}

/*
 * check-name: builtin-sync-fetch
 */
