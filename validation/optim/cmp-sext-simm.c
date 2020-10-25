#define sext(X)	((long long) (X))
#define POS	(1LL << 31)
#define NEG	(-POS - 1)

static int lt_ge0(int x) { return (sext(x) <  (POS + 0)) == 1; }
static int lt_ge1(int x) { return (sext(x) <  (POS + 1)) == 1; }
static int le_ge0(int x) { return (sext(x) <= (POS + 0)) == 1; }
static int le_ge1(int x) { return (sext(x) <= (POS + 1)) == 1; }
static int lt_lt0(int x) { return (sext(x) <  (NEG - 0)) == 1; }
static int lt_lt1(int x) { return (sext(x) <  (NEG - 1)) == 1; }
static int le_lt0(int x) { return (sext(x) <= (NEG - 0)) == 1; }
static int le_lt1(int x) { return (sext(x) <= (NEG - 1)) == 1; }

static int gt_ge0(int x) { return (sext(x) >  (POS + 0)) == 0; }
static int gt_ge1(int x) { return (sext(x) >  (POS + 1)) == 0; }
static int ge_ge0(int x) { return (sext(x) >= (POS + 0)) == 0; }
static int ge_ge1(int x) { return (sext(x) >= (POS + 1)) == 0; }
static int gt_lt0(int x) { return (sext(x) >  (NEG - 0)) == 0; }
static int gt_lt1(int x) { return (sext(x) >  (NEG - 1)) == 0; }
static int ge_lt0(int x) { return (sext(x) >= (NEG - 0)) == 0; }
static int ge_lt1(int x) { return (sext(x) >= (NEG - 1)) == 0; }

/*
 * check-name: cmp-sext-simm
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-returns: 1
 */
