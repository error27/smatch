int __access_ok(const void *addr, unsigned long size);

int test_access_ok(const void *addr, unsigned long count,
		   unsigned long size)
{
	return __access_ok(addr, count * size);
}

int test_ok(const void *addr, unsigned long size)
{
	return __access_ok(addr, size);
}

/*
 * check-name: smatch check access_ok math
 * check-command: smatch --no-data --spammy -p=kernel sm_check_access_ok_math.c
 *
 * check-output-start
sm_check_access_ok_math.c:6 test_access_ok() warn: math in access_ok() is dangerous 'count * size'
 * check-output-end
 */
