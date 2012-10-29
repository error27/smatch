void frob(void);

static int options_write(void)
{
	char a;
	unsigned char b;
	char c;

	a = (char)0xff;
	a = 0xff;
	(char)b = 0xff;
	b = 0xff;
	if (c > -400)
		frob();
	if (c < -400)
		frob();
	if (400 > c)
		frob();
	if (-400 > c)
		frob();
	b = -12;

}
/*
 * check-name: smatch cast handling
 * check-command: smatch sm_casts.c
 *
 * check-output-start
sm_casts.c:13 options_write() warn: (-400) is less than (-128) (min 'c' can be) so this is always true.
sm_casts.c:15 options_write() warn: (-400) is less than (-128) (min 'c' can be) so this is always false.
sm_casts.c:17 options_write() warn: 400 is more than 127 (max 'c' can be) so this is always true.
sm_casts.c:19 options_write() warn: (-400) is less than (-128) (min 'c' can be) so this is always false.
sm_casts.c:21 options_write() warn: assigning (-12) to unsigned variable 'b'
 * check-output-end
 */
