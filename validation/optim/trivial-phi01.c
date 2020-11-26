void foo(int a)
{
	if (a) {
		while (a) {
			switch (0) {
			default:
				a = 0;
			case 0:;
			}
		}
	}
}

/*
 * check-name: trivial-phi01
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: phi\\.
 */
