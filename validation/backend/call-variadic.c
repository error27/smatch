#define NULL	((void*)0)

extern int print(const char *msg, ...);

int foo(const char *fmt, int a, long l, int *p);
int foo(const char *fmt, int a, long l, int *p)
{
	return print(fmt, 'x', a, __LINE__, l, 0L, p, NULL);
}

/*
 * check-name: call-variadic
 * check-command: sparse-llvm-dis -m64 $file
 * check-output-ignore
 * check-output-contains: , ...) @print(\\(i8\\*\\|ptr\\) %ARG1., i32 120, i32 %ARG2., i32 8, i64 %ARG3., i64 0, \\(i32\\*\\|ptr\\) %ARG4., \\(i8\\*\\|ptr\\) null)
 * check-output-contains: define i32 @foo(
 * check-output-contains: declare i32 @print(
 *
 */
