void foo(void)
{
	int c = 1;
	switch (3) {
	case 0:
		do {
			;
	case 3:	;
		} while (c++);
	}
}

/*
 * check-name: bad-phisrc3
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-pattern(2): phisrc\\.
 * check-output-pattern(1): phi\\.
 */
