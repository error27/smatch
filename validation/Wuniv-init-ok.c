struct s {
	void *ptr;
};


static struct s s = { 0 };
static int a = { 0 };
static int b = { };
static int c = { 1, 2 };
static struct s *ptr = { 0 };

struct o {
	struct i {
		int a;
	};
};

static struct o o = { 0 };

/*
 * check-name: univ-init-ok
 * check-command: sparse -Wno-universal-initializer $file
 *
 * check-error-start
Wuniv-init-ok.c:8:16: error: invalid initializer
Wuniv-init-ok.c:9:16: error: invalid initializer
Wuniv-init-ok.c:10:26: warning: Using plain integer as NULL pointer
 * check-error-end
 */
