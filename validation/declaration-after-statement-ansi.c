static void func (int i)
{
	i;
	int j = i;
}
/*
 * check-name: declaration after statement (ANSI)
 * check-command: sparse -ansi $file
 * check-error-start
declaration-after-statement-ansi.c:4:2: warning: mixing declarations and code
 * check-error-end
 */
