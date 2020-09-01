typedef  __INT16_TYPE__ s16;
typedef  __INT32_TYPE__ s32;
typedef  __INT64_TYPE__ s64;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __UINT64_TYPE__ u64;

s16 s16s16(s16 a, s16 b) { a >>= b; return a; }
s16 s16s32(s16 a, s32 b) { a >>= b; return a; }
s16 s16s64(s16 a, s64 b) { a >>= b; return a; }
s16 s16u16(s16 a, u16 b) { a >>= b; return a; }
s16 s16u32(s16 a, u32 b) { a >>= b; return a; }
s16 s16u64(s16 a, u64 b) { a >>= b; return a; }
s32 s32s16(s32 a, s16 b) { a >>= b; return a; }
s32 s32s32(s32 a, s32 b) { a >>= b; return a; }
s32 s32s64(s32 a, s64 b) { a >>= b; return a; }
s32 s32u16(s32 a, u16 b) { a >>= b; return a; }
s32 s32u32(s32 a, u32 b) { a >>= b; return a; }
s32 s32u64(s32 a, u64 b) { a >>= b; return a; }
s64 s64s16(s64 a, s16 b);
s64 s64s32(s64 a, s32 b);
s64 s64s64(s64 a, s64 b) { a >>= b; return a; }
s64 s64u16(s64 a, u16 b) { a >>= b; return a; }
s64 s64u32(s64 a, u32 b) { a >>= b; return a; }
s64 s64u64(s64 a, u64 b) { a >>= b; return a; }
u16 u16s16(u16 a, s16 b) { a >>= b; return a; }
u16 u16s32(u16 a, s32 b) { a >>= b; return a; }
u16 u16s64(u16 a, s64 b) { a >>= b; return a; }
u16 u16u16(u16 a, u16 b) { a >>= b; return a; }
u16 u16u32(u16 a, u32 b) { a >>= b; return a; }
u16 u16u64(u16 a, u64 b) { a >>= b; return a; }
u32 u32s16(u32 a, s16 b) { a >>= b; return a; }
u32 u32s32(u32 a, s32 b) { a >>= b; return a; }
u32 u32s64(u32 a, s64 b) { a >>= b; return a; }
u32 u32u16(u32 a, u16 b) { a >>= b; return a; }
u32 u32u32(u32 a, u32 b) { a >>= b; return a; }
u32 u32u64(u32 a, u64 b) { a >>= b; return a; }
u64 u64s16(u64 a, s16 b);
u64 u64s32(u64 a, s32 b);
u64 u64s64(u64 a, s64 b) { a >>= b; return a; }
u64 u64u16(u64 a, u16 b) { a >>= b; return a; }
u64 u64u32(u64 a, u32 b) { a >>= b; return a; }
u64 u64u64(u64 a, u64 b) { a >>= b; return a; }

/*
 * check-name: shift-assign1
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
s16s16:
.L0:
	<entry-point>
	sext.32     %r2 <- (16) %arg2
	sext.32     %r4 <- (16) %arg1
	asr.32      %r5 <- %r4, %r2
	trunc.16    %r6 <- (32) %r5
	ret.16      %r6


s16s32:
.L2:
	<entry-point>
	sext.32     %r11 <- (16) %arg1
	asr.32      %r12 <- %r11, %arg2
	trunc.16    %r13 <- (32) %r12
	ret.16      %r13


s16s64:
.L4:
	<entry-point>
	trunc.32    %r17 <- (64) %arg2
	sext.32     %r19 <- (16) %arg1
	asr.32      %r20 <- %r19, %r17
	trunc.16    %r21 <- (32) %r20
	ret.16      %r21


s16u16:
.L6:
	<entry-point>
	zext.32     %r25 <- (16) %arg2
	sext.32     %r27 <- (16) %arg1
	asr.32      %r28 <- %r27, %r25
	trunc.16    %r29 <- (32) %r28
	ret.16      %r29


s16u32:
.L8:
	<entry-point>
	sext.32     %r34 <- (16) %arg1
	asr.32      %r35 <- %r34, %arg2
	trunc.16    %r36 <- (32) %r35
	ret.16      %r36


s16u64:
.L10:
	<entry-point>
	trunc.32    %r40 <- (64) %arg2
	sext.32     %r42 <- (16) %arg1
	asr.32      %r43 <- %r42, %r40
	trunc.16    %r44 <- (32) %r43
	ret.16      %r44


s32s16:
.L12:
	<entry-point>
	sext.32     %r48 <- (16) %arg2
	asr.32      %r50 <- %arg1, %r48
	ret.32      %r50


s32s32:
.L14:
	<entry-point>
	asr.32      %r55 <- %arg1, %arg2
	ret.32      %r55


s32s64:
.L16:
	<entry-point>
	trunc.32    %r59 <- (64) %arg2
	asr.32      %r61 <- %arg1, %r59
	ret.32      %r61


s32u16:
.L18:
	<entry-point>
	zext.32     %r65 <- (16) %arg2
	asr.32      %r67 <- %arg1, %r65
	ret.32      %r67


s32u32:
.L20:
	<entry-point>
	asr.32      %r72 <- %arg1, %arg2
	ret.32      %r72


s32u64:
.L22:
	<entry-point>
	trunc.32    %r76 <- (64) %arg2
	asr.32      %r78 <- %arg1, %r76
	ret.32      %r78


s64s64:
.L24:
	<entry-point>
	asr.64      %r83 <- %arg1, %arg2
	ret.64      %r83


s64u16:
.L26:
	<entry-point>
	zext.64     %r88 <- (16) %arg2
	asr.64      %r90 <- %arg1, %r88
	ret.64      %r90


s64u32:
.L28:
	<entry-point>
	zext.64     %r94 <- (32) %arg2
	asr.64      %r96 <- %arg1, %r94
	ret.64      %r96


s64u64:
.L30:
	<entry-point>
	asr.64      %r101 <- %arg1, %arg2
	ret.64      %r101


u16s16:
.L32:
	<entry-point>
	sext.32     %r105 <- (16) %arg2
	zext.32     %r107 <- (16) %arg1
	asr.32      %r108 <- %r107, %r105
	trunc.16    %r109 <- (32) %r108
	ret.16      %r109


u16s32:
.L34:
	<entry-point>
	zext.32     %r114 <- (16) %arg1
	asr.32      %r115 <- %r114, %arg2
	trunc.16    %r116 <- (32) %r115
	ret.16      %r116


u16s64:
.L36:
	<entry-point>
	trunc.32    %r120 <- (64) %arg2
	zext.32     %r122 <- (16) %arg1
	asr.32      %r123 <- %r122, %r120
	trunc.16    %r124 <- (32) %r123
	ret.16      %r124


u16u16:
.L38:
	<entry-point>
	zext.32     %r128 <- (16) %arg2
	zext.32     %r130 <- (16) %arg1
	asr.32      %r131 <- %r130, %r128
	trunc.16    %r132 <- (32) %r131
	ret.16      %r132


u16u32:
.L40:
	<entry-point>
	zext.32     %r137 <- (16) %arg1
	asr.32      %r138 <- %r137, %arg2
	trunc.16    %r139 <- (32) %r138
	ret.16      %r139


u16u64:
.L42:
	<entry-point>
	trunc.32    %r143 <- (64) %arg2
	zext.32     %r145 <- (16) %arg1
	asr.32      %r146 <- %r145, %r143
	trunc.16    %r147 <- (32) %r146
	ret.16      %r147


u32s16:
.L44:
	<entry-point>
	sext.32     %r151 <- (16) %arg2
	lsr.32      %r153 <- %arg1, %r151
	ret.32      %r153


u32s32:
.L46:
	<entry-point>
	lsr.32      %r158 <- %arg1, %arg2
	ret.32      %r158


u32s64:
.L48:
	<entry-point>
	trunc.32    %r162 <- (64) %arg2
	lsr.32      %r164 <- %arg1, %r162
	ret.32      %r164


u32u16:
.L50:
	<entry-point>
	zext.32     %r168 <- (16) %arg2
	lsr.32      %r170 <- %arg1, %r168
	ret.32      %r170


u32u32:
.L52:
	<entry-point>
	lsr.32      %r175 <- %arg1, %arg2
	ret.32      %r175


u32u64:
.L54:
	<entry-point>
	trunc.32    %r179 <- (64) %arg2
	lsr.32      %r181 <- %arg1, %r179
	ret.32      %r181


u64s64:
.L56:
	<entry-point>
	lsr.64      %r186 <- %arg1, %arg2
	ret.64      %r186


u64u16:
.L58:
	<entry-point>
	zext.64     %r191 <- (16) %arg2
	lsr.64      %r193 <- %arg1, %r191
	ret.64      %r193


u64u32:
.L60:
	<entry-point>
	zext.64     %r197 <- (32) %arg2
	lsr.64      %r199 <- %arg1, %r197
	ret.64      %r199


u64u64:
.L62:
	<entry-point>
	lsr.64      %r204 <- %arg1, %arg2
	ret.64      %r204


 * check-output-end
 */
