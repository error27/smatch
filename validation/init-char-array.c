/*
 * for array of char {<string>} gets special treatment in initializer.
 */
char *s[] = {"aaaaaaaaa"};
char t[][10] = {"aaaaaaaaa"};
char u[] = {"aaaaaaaaa"};
char v[] = "aaaaaaaaa";
void f(void)
{
	char x[1/(sizeof(s) == sizeof(char *))];
	char y[1/(sizeof(u) == 10)];
	char z[1/(sizeof(v) == 10)];
	char w[1/(sizeof(t) == 10)];
}

