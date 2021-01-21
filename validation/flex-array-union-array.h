struct s_flex {
	int i;
	long f[];
};

union s {
	struct s_flex flex;
	char buf[200];
};

static union s a[2];
