int foo(int a)
{
	void *label = a ? &&l1 : &&l2;
	goto *label;
l1:
	return a;
l2:
	return 0;
}

/*
 * check-name: cgoto02
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: %arg1
 */
