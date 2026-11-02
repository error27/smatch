struct big { int a, b; };
struct only_in_helper { int x; };
enum { ONE = 1 };

static int counter = 5;
int table[4] = { 1, 2, 3, 4 };
static const char name[] = "name";
static struct { int x; } anon = { 1 };

extern int external(int x);

static int helper(int x)
{
	struct only_in_helper o = { x };

	return external(o.x) + counter;
}

int exported(int x)
{
	return helper(x) + 1;
}
extern typeof(exported) exported;

static int target(struct big *b);

static int callee_of_inline(int x)
{
	return target(0) + x;
}

static inline int add_one(int x)
{
	return callee_of_inline(x) + ONE;
}

static int target(struct big *b)
{
	if (!b)
		return 0;
	return helper(b->a) + add_one(b->b) + table[1] + sizeof(name) +
	       exported(1) + anon.x;
}

/*
 * check-name: extract --minimal declares functions which are not inline
 * check-command: extract --func=target --minimal $file && cat $file.extract.target && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-minimal.c.extract.target
typedef _Bool bool;

struct big;
struct big {
	int a;
	int b;
};
extern int helper(int x);
extern int callee_of_inline(int x);
static inline int add_one(int x)
{

	return callee_of_inline(x) + 1;
}
extern int table[4];
static const char name[] = "name";
extern int exported(int x);
static struct {
	int x;
} anon = {
	1,
};
static int target(struct big *b)
{

	if ((!b))
		return 0;
	return helper((b->a)) + add_one((b->b)) + table[1] + sizeof((name)) + exported(1) + (anon.x);
}

 * check-output-end
 */
