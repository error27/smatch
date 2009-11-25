void free(void *ptr);
void frob(void *ptr);

int x,y,z;
void func (void)
{
	if (x)
		free(x);
	if (y)
		frob(y);
	free(y);
	if (z) {
		free(z);
	}
}


/*
 * check-name: Redundant NULL check
 * check-command: smatch sm_redundant_check.c
 *
 * check-output-start
sm_redundant_check.c +8 func(3) warn: redundant null check on x calling free()
sm_redundant_check.c +13 func(8) warn: redundant null check on z calling free()
 * check-output-end
 */
