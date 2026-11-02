#ifdef __CHECKER__
#error "extract output is for GCC, not for the checker"
#endif

int test(int i)
{
	return i;
}

/*
 * check-name: extract does not define __CHECKER__
 * check-command: extract --func=test $file
 *
 * check-output-start
extract: wrote extract-no-checker.c.extract.test
 * check-output-end
 */
