struct S {
	         int  :1;
	  signed int s:2;
	unsigned int u:3;
	        long l;
	      double d;
};

int os(int i, struct S *b) { return i || b->s; }
int ou(int i, struct S *b) { return i || b->u; }
int ol(int i, struct S *b) { return i || b->l; }
int od(int i, struct S *b) { return i || b->d; }

int as(int i, struct S *b) { return i && b->s; }
int au(int i, struct S *b) { return i && b->u; }
int al(int i, struct S *b) { return i && b->l; }
int ad(int i, struct S *b) { return i && b->d; }

/*
 * check-name: logical
 * check-command: test-linearize -m64 -fdump-ir -Wno-decl $file
 *
 * check-output-start
os:
.L0:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	phisrc.32   %phi1 <- $1
	load.32     %r1 <- 0[i]
	setne.1     %r2 <- %r1, $0
	cbr         %r2, .L3, .L2

.L2:
	load.64     %r3 <- 0[b]
	load.32     %r4 <- 0[%r3]
	lsr.32      %r5 <- %r4, $1
	trunc.2     %r6 <- (32) %r5
	setne.1     %r7 <- %r6, $0
	zext.32     %r8 <- (1) %r7
	phisrc.32   %phi2 <- %r8
	br          .L3

.L3:
	phi.32      %r9 <- %phi1, %phi2
	phisrc.32   %phi3(return) <- %r9
	br          .L1

.L1:
	phi.32      %r10 <- %phi3(return)
	ret.32      %r10


ou:
.L4:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	phisrc.32   %phi4 <- $1
	load.32     %r11 <- 0[i]
	setne.1     %r12 <- %r11, $0
	cbr         %r12, .L7, .L6

.L6:
	load.64     %r13 <- 0[b]
	load.32     %r14 <- 0[%r13]
	lsr.32      %r15 <- %r14, $3
	trunc.3     %r16 <- (32) %r15
	setne.1     %r17 <- %r16, $0
	zext.32     %r18 <- (1) %r17
	phisrc.32   %phi5 <- %r18
	br          .L7

.L7:
	phi.32      %r19 <- %phi4, %phi5
	phisrc.32   %phi6(return) <- %r19
	br          .L5

.L5:
	phi.32      %r20 <- %phi6(return)
	ret.32      %r20


ol:
.L8:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	phisrc.32   %phi7 <- $1
	load.32     %r21 <- 0[i]
	setne.1     %r22 <- %r21, $0
	cbr         %r22, .L11, .L10

.L10:
	load.64     %r23 <- 0[b]
	load.64     %r24 <- 8[%r23]
	setne.1     %r25 <- %r24, $0
	zext.32     %r26 <- (1) %r25
	phisrc.32   %phi8 <- %r26
	br          .L11

.L11:
	phi.32      %r27 <- %phi7, %phi8
	phisrc.32   %phi9(return) <- %r27
	br          .L9

.L9:
	phi.32      %r28 <- %phi9(return)
	ret.32      %r28


od:
.L12:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	phisrc.32   %phi10 <- $1
	load.32     %r29 <- 0[i]
	setne.1     %r30 <- %r29, $0
	cbr         %r30, .L15, .L14

.L14:
	load.64     %r31 <- 0[b]
	load.64     %r32 <- 16[%r31]
	setfval.64  %r33 <- 0.000000e+00
	fcmpune.1   %r34 <- %r32, %r33
	zext.32     %r35 <- (1) %r34
	phisrc.32   %phi11 <- %r35
	br          .L15

.L15:
	phi.32      %r36 <- %phi10, %phi11
	phisrc.32   %phi12(return) <- %r36
	br          .L13

.L13:
	phi.32      %r37 <- %phi12(return)
	ret.32      %r37


as:
.L16:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	phisrc.32   %phi13 <- $0
	load.32     %r38 <- 0[i]
	setne.1     %r39 <- %r38, $0
	cbr         %r39, .L18, .L19

.L18:
	load.64     %r40 <- 0[b]
	load.32     %r41 <- 0[%r40]
	lsr.32      %r42 <- %r41, $1
	trunc.2     %r43 <- (32) %r42
	setne.1     %r44 <- %r43, $0
	zext.32     %r45 <- (1) %r44
	phisrc.32   %phi14 <- %r45
	br          .L19

.L19:
	phi.32      %r46 <- %phi14, %phi13
	phisrc.32   %phi15(return) <- %r46
	br          .L17

.L17:
	phi.32      %r47 <- %phi15(return)
	ret.32      %r47


au:
.L20:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	phisrc.32   %phi16 <- $0
	load.32     %r48 <- 0[i]
	setne.1     %r49 <- %r48, $0
	cbr         %r49, .L22, .L23

.L22:
	load.64     %r50 <- 0[b]
	load.32     %r51 <- 0[%r50]
	lsr.32      %r52 <- %r51, $3
	trunc.3     %r53 <- (32) %r52
	setne.1     %r54 <- %r53, $0
	zext.32     %r55 <- (1) %r54
	phisrc.32   %phi17 <- %r55
	br          .L23

.L23:
	phi.32      %r56 <- %phi17, %phi16
	phisrc.32   %phi18(return) <- %r56
	br          .L21

.L21:
	phi.32      %r57 <- %phi18(return)
	ret.32      %r57


al:
.L24:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	phisrc.32   %phi19 <- $0
	load.32     %r58 <- 0[i]
	setne.1     %r59 <- %r58, $0
	cbr         %r59, .L26, .L27

.L26:
	load.64     %r60 <- 0[b]
	load.64     %r61 <- 8[%r60]
	setne.1     %r62 <- %r61, $0
	zext.32     %r63 <- (1) %r62
	phisrc.32   %phi20 <- %r63
	br          .L27

.L27:
	phi.32      %r64 <- %phi20, %phi19
	phisrc.32   %phi21(return) <- %r64
	br          .L25

.L25:
	phi.32      %r65 <- %phi21(return)
	ret.32      %r65


ad:
.L28:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	phisrc.32   %phi22 <- $0
	load.32     %r66 <- 0[i]
	setne.1     %r67 <- %r66, $0
	cbr         %r67, .L30, .L31

.L30:
	load.64     %r68 <- 0[b]
	load.64     %r69 <- 16[%r68]
	setfval.64  %r70 <- 0.000000e+00
	fcmpune.1   %r71 <- %r69, %r70
	zext.32     %r72 <- (1) %r71
	phisrc.32   %phi23 <- %r72
	br          .L31

.L31:
	phi.32      %r73 <- %phi23, %phi22
	phisrc.32   %phi24(return) <- %r73
	br          .L29

.L29:
	phi.32      %r74 <- %phi24(return)
	ret.32      %r74


 * check-output-end
 */
