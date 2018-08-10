int foo(int x)
{
	return (x | 0xfffff000) & 0xfff;
}

/*
 * check-name: and-or-constant0
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-excludes: or\\.
 */
