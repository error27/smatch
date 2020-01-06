void fun(void);

void foo(int *p)
{
	for (*p; *p; *p) {
l:
		fun();
	}

	if (0)
		goto l;
}

/*
 * check-name: kill-dead-loads00
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: phi\\.
 * check-output-pattern(1): load\\.
 * check-output-end
 */
