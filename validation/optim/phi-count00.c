inline int inl(int d, int e, int f)
{
	switch (d) {
	case 0:
		return e;
	case 1:
		return f;
	default:
		return 0;
	}
}

void foo(int a, int b, int c)
{
	while (1) {
		if (inl(a, b, c))
			break;
	}
}

/*
 * check-name: phi-count00
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-pattern(0,2): phisrc
 */
