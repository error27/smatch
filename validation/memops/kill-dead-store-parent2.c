int ladder02(int *ptr, int p, int x)
{
	*ptr = x++;
	if (p)
		goto l11;
	else
		goto l12;
l11:
	*ptr = x++;
	goto l20;
l12:
	*ptr = x++;
	goto l20;
l20:
	*ptr = x++;
	return *ptr;
}

/*
 * check-name: kill-dead-store-parent2
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-pattern(1): store
 */
