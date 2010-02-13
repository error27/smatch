int a[] = {1, 2, 3, 4};
char *b = "abc";
char c[4];
char d[4] = "";

int x;
static int options_write(void)
{
	int i;
	char *str = b;
	char *str2 = "123";
	char *str3;
	char *str4;
	char *str5;

	str3 = str2;
	str4 = str;
	if (x)
		str5 = "asdf";
	else
		str5 = "aa";

	for (i = 0; i < 4 && frob(); i++)
		;
	a[i] = 42;
	b[i] = '\0';
	c[i] = '\0';
	d[i] = '\0';
	str[i] = '\0';
	str2[i] = '\0';
	str3[i] = '\0';
	str4[i] = '\0';
	str5[i] = '\0';
}
/*
 * check-name: smatch array check
 * check-command: smatch sm_array_overflow.c
 *
 * check-output-start
sm_array_overflow.c +25 options_write(18) error: buffer overflow 'a' 4 <= 4
sm_array_overflow.c +26 options_write(19) error: buffer overflow 'b' 4 <= 4
sm_array_overflow.c +27 options_write(20) error: buffer overflow 'c' 4 <= 4
sm_array_overflow.c +28 options_write(21) error: buffer overflow 'd' 4 <= 4
sm_array_overflow.c +29 options_write(22) error: buffer overflow 'str' 4 <= 4
sm_array_overflow.c +30 options_write(23) error: buffer overflow 'str2' 4 <= 4
sm_array_overflow.c +31 options_write(24) error: buffer overflow 'str3' 4 <= 4
sm_array_overflow.c +32 options_write(25) error: buffer overflow 'str4' 4 <= 4
 * check-output-end
 */
