int def(void);

int fun4(struct xfrm_state *net, int cnt)
{
	int err = 0;
	if (err)
		goto out;
	for (; net;)
		err = def();
	if (cnt)
out:
		return err;
	return 0;
}

/*
 * check-name: bad-phisrc1a
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: select\\.
 */

