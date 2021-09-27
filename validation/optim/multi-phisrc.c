void fun(void);

void foo(int p, int a)
{
	if (p == p) {
		switch (p) {
		case 0:
			break;
		case 1:
			a = 0;
		}
	}
	if (a)
		fun();
}

/*
 * check-name: multi-phisrc
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: phi
 */
