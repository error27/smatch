void abort(void) __attribute__((noreturn));

int bar(int a)
{
	return a ? (abort(), 0) : 0;
}

int qux(int a)
{
	return a ? (abort(), 0) : (abort(), 1);
}

/*
 * check-name: join-cond-discard
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: phisrc\\..*phi
 */
