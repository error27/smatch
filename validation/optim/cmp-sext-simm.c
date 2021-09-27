#define sext(X)	((long long) (X))
#define POS	(1LL << 31)
#define NEG	(-POS - 1)

static int lt_ge0(int x) { return (sext(x) <  (POS + 0)) == 1; }
static int lt_ge1(int x) { return (sext(x) <  (POS + 1)) == 1; }
static int lt_ge2(int x) { return (sext(x) <  (POS + 2)) == 1; }
static int lt_gex(int x) { return (sext(x) <  (POS<< 1)) == 1; }
static int lt_gey(int x) { return (sext(x) <  (POS<< 3)) == 1; }
static int le_ge0(int x) { return (sext(x) <= (POS + 0)) == 1; }
static int le_ge1(int x) { return (sext(x) <= (POS + 1)) == 1; }
static int le_ge2(int x) { return (sext(x) <= (POS + 2)) == 1; }
static int le_gex(int x) { return (sext(x) <= (POS<< 1)) == 1; }
static int le_gey(int x) { return (sext(x) <= (POS<< 3)) == 1; }
static int ge_ge0(int x) { return (sext(x) >= (POS + 0)) == 0; }
static int ge_ge1(int x) { return (sext(x) >= (POS + 1)) == 0; }
static int ge_ge2(int x) { return (sext(x) >= (POS + 2)) == 0; }
static int ge_gex(int x) { return (sext(x) >= (POS<< 1)) == 0; }
static int ge_gey(int x) { return (sext(x) >= (POS<< 3)) == 0; }
static int gt_ge0(int x) { return (sext(x) >  (POS + 0)) == 0; }
static int gt_ge1(int x) { return (sext(x) >  (POS + 1)) == 0; }
static int gt_ge2(int x) { return (sext(x) >  (POS + 2)) == 0; }
static int gt_gex(int x) { return (sext(x) >  (POS<< 1)) == 0; }
static int gt_gey(int x) { return (sext(x) >  (POS<< 3)) == 0; }

static int lt_lt0(int x) { return (sext(x) <  (NEG - 0)) == 0; }
static int lt_lt1(int x) { return (sext(x) <  (NEG - 1)) == 0; }
static int lt_lt2(int x) { return (sext(x) <  (NEG - 2)) == 0; }
static int lt_ltx(int x) { return (sext(x) <  (NEG<< 1)) == 0; }
static int lt_lty(int x) { return (sext(x) <  (NEG<< 3)) == 0; }
static int le_lt0(int x) { return (sext(x) <= (NEG - 0)) == 0; }
static int le_lt1(int x) { return (sext(x) <= (NEG - 1)) == 0; }
static int le_lt2(int x) { return (sext(x) <= (NEG - 2)) == 0; }
static int le_ltx(int x) { return (sext(x) <= (NEG<< 1)) == 0; }
static int le_lty(int x) { return (sext(x) <= (NEG<< 3)) == 0; }
static int ge_lt0(int x) { return (sext(x) >= (NEG - 0)) == 1; }
static int ge_lt1(int x) { return (sext(x) >= (NEG - 1)) == 1; }
static int ge_lt2(int x) { return (sext(x) >= (NEG - 2)) == 1; }
static int ge_ltx(int x) { return (sext(x) >= (NEG<< 1)) == 1; }
static int ge_lty(int x) { return (sext(x) >= (NEG<< 3)) == 1; }
static int gt_lt0(int x) { return (sext(x) >  (NEG - 0)) == 1; }
static int gt_lt1(int x) { return (sext(x) >  (NEG - 1)) == 1; }
static int gt_lt2(int x) { return (sext(x) >  (NEG - 2)) == 1; }
static int gt_ltx(int x) { return (sext(x) >  (NEG<< 1)) == 1; }
static int gt_lty(int x) { return (sext(x) >  (NEG<< 3)) == 1; }

/*
 * check-name: cmp-sext-simm
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
