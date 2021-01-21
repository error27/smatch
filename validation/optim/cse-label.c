int foo(void)
{
label:
	return &&label == &&label;
}

/*
 * check-name: cse-label
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
