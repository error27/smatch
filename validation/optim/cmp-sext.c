#define T(TYPE)			__##TYPE##_TYPE__
#define cmp(TYPE, X, OP, Y)	((T(TYPE)) X OP (T(TYPE)) Y)
#define TEST(T1, T2, X, OP, Y)	cmp(T1, X, OP, Y) == cmp(T2, X, OP, Y)

#define ARGS(TYPE)	T(TYPE) a, T(TYPE)b

_Bool cmpe_sextp(ARGS(INT32)) { return TEST(UINT64, UINT32, a, ==, 0x7fffffff); }
_Bool cmps_sextp(ARGS(INT32)) { return TEST( INT64,  INT32, a, < , 0x7fffffff); }
_Bool cmpu_sextp(ARGS(INT32)) { return TEST(UINT64, UINT32, a, < , 0x7fffffff); }
_Bool cmpe_sextn(ARGS(INT32)) { return TEST(UINT64, UINT32, a, ==, -1); }
_Bool cmps_sextn(ARGS(INT32)) { return TEST( INT64,  INT32, a, < , -1); }
_Bool cmpu_sextn(ARGS(INT32)) { return TEST(UINT64, UINT32, a, < , -1); }

_Bool cmpltu_sext(int a) { return (a <  0x80000000ULL) == (a >= 0); }
_Bool cmpgtu_sext(int a) { return (a >= 0x80000000ULL) == (a <  0); }

/*
 * check-name: cmp-sext
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
