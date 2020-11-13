void bar(int);

void foo(void)
{
	char buf[1] = { 42 };
	const char *p = buf;
	const char **q = &p;
	int ch = 0;
	switch (**q) {
	case 4:
		ch = 2;
	}
	bar(ch);
}

/*
 * check-name: memops-missed01
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: store\\.
 * check-output-excludes: load\\.
 */
