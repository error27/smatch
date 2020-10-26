#define T(TYPE)			__##TYPE##_TYPE__
#define cmp(TYPE, X, OP, Y)	((T(TYPE)) X OP (T(TYPE)) Y)
#define TEST(T1, T2, X, OP, Y)	cmp(T1, X, OP, Y) == cmp(T2, X, OP, Y)

#define ARGS(TYPE)	T(TYPE) a, T(TYPE)b

_Bool cmpe_zext(ARGS(UINT32)) { return TEST(UINT64, UINT32, a, ==, 0xffffffff); }
_Bool cmps_zext(ARGS(UINT32)) { return TEST( INT64, UINT32, a, < , 0xffffffff); }
_Bool cmpu_zext(ARGS(UINT32)) { return TEST(UINT64, UINT32, a, < , 0xffffffff); }

/*
 * check-name: cmp-zext
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
