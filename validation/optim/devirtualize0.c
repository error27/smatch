static inline long f1(long x) { return x + 1;}

extern long foo(long a);
long foo(long a)
{
	typeof(f1) *f = f1;
	return f(a);
}

/*
 * check-name: devirtualize0
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-excludes: call\\.
 */
