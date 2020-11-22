void abort(void) __attribute__((__noreturn__));

int foo(int a)
{
	void *label;

	if (a == a)
		label = &&L1;
	else
		label = &&L2;
	goto *label;
L1:	return 0;
L2:	abort();
}

/*
 * check-name: cgoto01
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: set\\.
 * check-output-excludes: jmp
 * check-output-excludes: call
 */
