int foo(int p)
{
	int t = (p ? 42 : 0);
	return (t ? 42 : 0) == ( p ? 42 : 0);
}

/*
 * check-name: select-select-true-false1
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
