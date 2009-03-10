//#include <stdio.h>

struct foo {
	int a;
};

static void func (struct foo *aa)
{
	aa->a = 1;
}

void func1 (struct foo *ab)
{
	if (!ab)
		printf("Error ad is not ALLOWED to be NULL\n");
	ab->a = 1;
}

void bar (void)
{
	struct foo *ac = returns_nonnull(sizeof(*ac));
	struct foo *ad;

	if(1)
		ac = 0;
	func(ac);
	func1(ad);
	func(ad);
}
/*
 * check-name: Cross function dereferences
 * check-command: smatch sm_params.c
 *
 * check-output-start
sm_params.c +16 info: unchecked param func1 0
sm_params.c +26 error: cross_func deref func 0
sm_params.c +27 error: cross_func deref func1 0
sm_params.c +28 error: cross_func deref func 0
 * check-output-end
 */
