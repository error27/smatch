void something();

int kmap(int p);
int kunmap(int p);
int kmap_atomic(int p);
int kunmap_atomic(int p);

void func(void)
{
	int page;
	int x;
	int y;
	int z;

	x = kmap(page);
	kunmap(page);
	kunmap(x);
	y = kmap_atomic(z);
	kunmap_atomic(y);
	kunmap_atomic(z);
}
/*
 * check-name: smatch check kunmap
 * check-command: smatch -p=kernel sm_check_kunmap.c
 *
 * check-output-start
sm_check_kunmap.c +17 func(9) warn: passing the wrong stuff kunmap()
sm_check_kunmap.c +20 func(12) warn: passing the wrong stuff to kmap_atomic()
 * check-output-end
 */
