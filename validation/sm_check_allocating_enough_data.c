void *malloc(unsigned long size);
void use(void *p);

struct foo {
	int a;
	int b;
};

void *alloc_one(void)
{
	return malloc(1);
}

void test(void)
{
	struct foo *bad;
	struct foo *good;

	bad = alloc_one();
	good = malloc(sizeof(*good));
	use(bad);
	use(good);
}

/*
 * check-name: smatch: check allocating enough data
 * check-command: validation/smatch_db_test.sh sm_check_allocating_enough_data.c
 *
 * check-output-start
sm_check_allocating_enough_data.c:19 test() error: not allocating enough for = 'bad' 8 vs 1
 * check-output-end
 */
