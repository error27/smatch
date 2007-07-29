static char a[sizeof(char *) + 1];
static char b[1/(sizeof(a) - sizeof(0,a))];
/*
 * check-name: Comma and array decay
 * check-description: arguments of comma should degenerate
 */
