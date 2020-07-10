static int foo(int a)
{
	goto label;
	switch(a) {
	default:
label:
		break;
	}
	return 0;
}

/*
 * check-name: label-unreachable
 * check-command: test-linearize $file
 *
 * check-error-ignore
 * check-output-ignore
 * check-output-contains: ret\\.
 * check-output-excludes: END
 */
