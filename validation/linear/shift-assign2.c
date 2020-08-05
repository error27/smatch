typedef  __INT16_TYPE__ s16;
typedef  __INT32_TYPE__ s32;
typedef  __INT64_TYPE__ s64;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __UINT64_TYPE__ u64;

s64 s64s16(s64 a, s16 b) { a >>= b; return a; }
s64 s64s32(s64 a, s32 b) { a >>= b; return a; }
u64 u64s16(u64 a, s16 b) { a >>= b; return a; }
u64 u64s32(u64 a, s32 b) { a >>= b; return a; }

/*
 * check-name: shift-assign2
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
s64s16:
.L0:
	<entry-point>
	sext.32     %r2 <- (16) %arg2
	zext.64     %r3 <- (32) %r2
	asr.64      %r5 <- %arg1, %r3
	ret.64      %r5


s64s32:
.L2:
	<entry-point>
	zext.64     %r9 <- (32) %arg2
	asr.64      %r11 <- %arg1, %r9
	ret.64      %r11


u64s16:
.L4:
	<entry-point>
	sext.32     %r15 <- (16) %arg2
	zext.64     %r16 <- (32) %r15
	lsr.64      %r18 <- %arg1, %r16
	ret.64      %r18


u64s32:
.L6:
	<entry-point>
	zext.64     %r22 <- (32) %arg2
	lsr.64      %r24 <- %arg1, %r22
	ret.64      %r24


 * check-output-end
 */
