int sext(int x)
{
	return (x << 5) >> 5;
}

/*
 * check-name: sext
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: sext\\.
 * check-output-contains: asr\\.32
 * check-output-contains: shl\\.32
 */
