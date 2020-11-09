#define T(TYPE)			__##TYPE##_TYPE__
#define cmp(TYPE, X, OP, Y)	((T(TYPE)) X OP (T(TYPE)) Y)
#define TEST(T1, T2, X, OP, Y)	cmp(T1, X, OP, Y) == cmp(T2, X, OP, Y)

#define ARGS(TYPE)	T(TYPE) a, T(TYPE)b

_Bool cmpe_sext_sext(ARGS(INT32)) { return TEST(UINT64, UINT32, a, ==, b); }
_Bool cmps_sext_sext(ARGS(INT32)) { return TEST( INT64,  INT32, a, < , b); }
_Bool cmpu_sext_sext(ARGS(INT32)) { return TEST(UINT64, UINT32, a, < , b); }

/*
 * check-name: cmp-sext-sext
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
