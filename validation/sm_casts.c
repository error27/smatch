static int options_write(void)
{
	char a;
	unsigned char b;

	a = (char)0xff;
	a = 0xff;
	(char)b = 0xff;
	b = 0xff;
}
/*
 * check-name: smatch cast handling
 * check-command: smatch --spammy sm_casts.c
 *
 * check-output-start
sm_casts.c +7 options_write(6) error: value 255 can't fit into 127 a
sm_casts.c +8 options_write(7) error: value 255 can't fit into 127 b
 * check-output-end
 */
