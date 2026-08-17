struct foo {
	char buf[8];
};

int test(struct foo *p)
{
	if (p->buf)
		return 1;
	if (p->buf[0])
		return 2;
	return 0;
}

/*
 * check-name: smatch: check array condition
 * check-command: smatch --no-data sm_check_array_condition.c
 *
 * check-output-start
sm_check_array_condition.c:7 test() warn: this array is probably non-NULL. 'p->buf'
 * check-output-end
 */
