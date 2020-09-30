struct s {
	int i;
	long f[];
};

static struct s a[2];

/*
 * check-name: flex-array-array
 * check-command: sparse -Wflexible-array-array $file
 *
 * check-error-start
flex-array-array.c:6:18: warning: array of flexible structures
 * check-error-end
 */
