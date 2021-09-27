typedef __INT32_TYPE__ int32;
typedef __INT64_TYPE__ int64;

static _Bool sfoo(int64 a) { return ((int32) ~a) == (~ (int32)a); }
static _Bool sbar(int64 a) { return (~(int32) ~a) == (int32)a; }


typedef __UINT32_TYPE__ uint32;
typedef __UINT64_TYPE__ uint64;

static _Bool ufoo(uint64 a) { return ((uint32) ~a) == (~ (uint32)a); }
static _Bool ubar(uint64 a) { return (~(uint32) ~a) == (uint32)a; }

/*
 * check-name: trunc-not0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
