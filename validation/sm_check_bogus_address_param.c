struct foo {
	int padding;
	int member;
};

void use(void *p);

void test(struct foo *p)
{
	if (!p)
		use(&p->member);
	else
		use(p);
}

/*
 * check-name: smatch: check bogus address parameter
 * check-command: smatch --no-data sm_check_bogus_address_param.c
 *
 * check-output-start
sm_check_bogus_address_param.c:11 test() warn: address of NULL pointer 'p'
 * check-output-end
 */
