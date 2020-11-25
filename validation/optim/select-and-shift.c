#define S1	2
#define S2	5
#define S	(S2 - S1)

#define	A	(1 << S1)
#define	B	(1 << S2)

int foo(int p) { return ((p & A) ? B : 0) == ((((unsigned)p) & A) << S); }
int bar(int p) { return ((p & B) ? A : 0) == ((((unsigned)p) & B) >> S); }

/*
 * check-name: select-and-shift
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
