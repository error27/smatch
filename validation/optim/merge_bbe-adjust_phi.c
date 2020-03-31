extern int array[2];

static inline int stupid_select(int idx)
{
	if (idx)
		idx = 0;
	return array[idx];
}

int select(void)
{
	int d = stupid_select(-1);
	return d;
}

/*
 * check-name: merge_bbe-adjust_phi
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: phisrc\\.
 * check-output-excludes: phi\\.
 */
