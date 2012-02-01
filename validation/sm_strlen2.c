int strlen(char *str);
int strcpy(char *str);

void func (char *input1, char *input2, char *input3)
{
	char buf[4];

	if (strlen(input1) > 4)
		return;
	strcpy(buf, input1);

	if (10 > strlen(input2))
		strcpy(buf, input2);

	if (strlen(input3) <= 4)
		strcpy(buf, input3);
}
/*
 * check-name: Smatch strlen test #2
 * check-command: smatch sm_strlen2.c
 *
 * check-output-start
sm_strlen2.c:10 func(6) error: strcpy() 'input1' too large for 'buf' (5 vs 4)
sm_strlen2.c:13 func(9) error: strcpy() 'input2' too large for 'buf' (10 vs 4)
sm_strlen2.c:16 func(12) error: strcpy() 'input3' too large for 'buf' (5 vs 4)
 * check-output-end
 */
