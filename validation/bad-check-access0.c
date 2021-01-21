#define SIZE	2
static int buf[SIZE];

static inline int swt(int i)
{
	switch (i) {
	case 0 ... (SIZE-1):
		return buf[i];
	default:
		return 0;
	}
}

static int switch_ok(void) { return swt(1); }
static int switch_ko(void) { return swt(2); }


static inline int cbr(int i, int p)
{
	if (p)
		return buf[i];
	else
		return 0;
}

static int branch_ok(int x) { return cbr(1, x != x); }
static int branch_ko(int x) { return cbr(2, x != x); }

/*
 * check-name: bad-check-access0
 */
