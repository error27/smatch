struct s {
	void *ptr;
};


static struct s s = { 0 };

/*
 * check-name: univ-init-ok
 * check-command: sparse -Wno-universal-initializer $file
 */
