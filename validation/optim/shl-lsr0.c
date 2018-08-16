unsigned mask(unsigned x)
{
	return (x >> 15) << 15;
}

/*
 * check-name: shl-lsr0
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-contains: and\\..*0xffff8000
 * check-output-excludes: lsr\\.
 * check-output-excludes: shl\\.
 */
