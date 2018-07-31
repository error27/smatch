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
	load.32     %r1 <- 0[i]
	setne.1     %r2 <- %r1, $0
	cbr         %r2, .L2, .L3

.L2:
	setne.1     %r3 <- $1, $0
	zext.32     %r4 <- (1) %r3
	phisrc.32   %phi1 <- %r4
	br          .L4

.L3:
	load.64     %r5 <- 0[b]
	load.32     %r6 <- 0[%r5]
	lsr.32      %r7 <- %r6, $1
	trunc.2     %r8 <- (32) %r7
	setne.1     %r9 <- %r8, $0
	zext.32     %r10 <- (1) %r9
	phisrc.32   %phi2 <- %r10
	br          .L4

.L4:
	phi.32      %r11 <- %phi1, %phi2
	phisrc.32   %phi3(return) <- %r11
	br          .L1

.L1:
	phi.32      %r12 <- %phi3(return)
	ret.32      %r12


ou:
.L5:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	load.32     %r13 <- 0[i]
	setne.1     %r14 <- %r13, $0
	cbr         %r14, .L7, .L8

.L7:
	setne.1     %r15 <- $1, $0
	zext.32     %r16 <- (1) %r15
	phisrc.32   %phi5 <- %r16
	br          .L9

.L8:
	load.64     %r17 <- 0[b]
	load.32     %r18 <- 0[%r17]
	lsr.32      %r19 <- %r18, $3
	trunc.3     %r20 <- (32) %r19
	setne.1     %r21 <- %r20, $0
	zext.32     %r22 <- (1) %r21
	phisrc.32   %phi6 <- %r22
	br          .L9

.L9:
	phi.32      %r23 <- %phi5, %phi6
	phisrc.32   %phi7(return) <- %r23
	br          .L6

.L6:
	phi.32      %r24 <- %phi7(return)
	ret.32      %r24


ol:
.L10:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	load.32     %r25 <- 0[i]
	setne.1     %r26 <- %r25, $0
	cbr         %r26, .L12, .L13

.L12:
	setne.1     %r27 <- $1, $0
	zext.32     %r28 <- (1) %r27
	phisrc.32   %phi9 <- %r28
	br          .L14

.L13:
	load.64     %r29 <- 0[b]
	load.64     %r30 <- 8[%r29]
	setne.1     %r31 <- %r30, $0
	zext.32     %r32 <- (1) %r31
	phisrc.32   %phi10 <- %r32
	br          .L14

.L14:
	phi.32      %r33 <- %phi9, %phi10
	phisrc.32   %phi11(return) <- %r33
	br          .L11

.L11:
	phi.32      %r34 <- %phi11(return)
	ret.32      %r34


od:
.L15:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	load.32     %r35 <- 0[i]
	setne.1     %r36 <- %r35, $0
	cbr         %r36, .L17, .L18

.L17:
	setne.1     %r37 <- $1, $0
	zext.32     %r38 <- (1) %r37
	phisrc.32   %phi13 <- %r38
	br          .L19

.L18:
	load.64     %r39 <- 0[b]
	load.64     %r40 <- 16[%r39]
	setfval.64  %r41 <- 0.000000e+00
	fcmpune.1   %r42 <- %r40, %r41
	zext.32     %r43 <- (1) %r42
	phisrc.32   %phi14 <- %r43
	br          .L19

.L19:
	phi.32      %r44 <- %phi13, %phi14
	phisrc.32   %phi15(return) <- %r44
	br          .L16

.L16:
	phi.32      %r45 <- %phi15(return)
	ret.32      %r45


as:
.L20:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	load.32     %r46 <- 0[i]
	setne.1     %r47 <- %r46, $0
	cbr         %r47, .L22, .L23

.L22:
	load.64     %r48 <- 0[b]
	load.32     %r49 <- 0[%r48]
	lsr.32      %r50 <- %r49, $1
	trunc.2     %r51 <- (32) %r50
	setne.1     %r52 <- %r51, $0
	zext.32     %r53 <- (1) %r52
	phisrc.32   %phi17 <- %r53
	br          .L24

.L23:
	setne.1     %r54 <- $0, $0
	zext.32     %r55 <- (1) %r54
	phisrc.32   %phi18 <- %r55
	br          .L24

.L24:
	phi.32      %r56 <- %phi17, %phi18
	phisrc.32   %phi19(return) <- %r56
	br          .L21

.L21:
	phi.32      %r57 <- %phi19(return)
	ret.32      %r57


au:
.L25:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	load.32     %r58 <- 0[i]
	setne.1     %r59 <- %r58, $0
	cbr         %r59, .L27, .L28

.L27:
	load.64     %r60 <- 0[b]
	load.32     %r61 <- 0[%r60]
	lsr.32      %r62 <- %r61, $3
	trunc.3     %r63 <- (32) %r62
	setne.1     %r64 <- %r63, $0
	zext.32     %r65 <- (1) %r64
	phisrc.32   %phi21 <- %r65
	br          .L29

.L28:
	setne.1     %r66 <- $0, $0
	zext.32     %r67 <- (1) %r66
	phisrc.32   %phi22 <- %r67
	br          .L29

.L29:
	phi.32      %r68 <- %phi21, %phi22
	phisrc.32   %phi23(return) <- %r68
	br          .L26

.L26:
	phi.32      %r69 <- %phi23(return)
	ret.32      %r69


al:
.L30:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	load.32     %r70 <- 0[i]
	setne.1     %r71 <- %r70, $0
	cbr         %r71, .L32, .L33

.L32:
	load.64     %r72 <- 0[b]
	load.64     %r73 <- 8[%r72]
	setne.1     %r74 <- %r73, $0
	zext.32     %r75 <- (1) %r74
	phisrc.32   %phi25 <- %r75
	br          .L34

.L33:
	setne.1     %r76 <- $0, $0
	zext.32     %r77 <- (1) %r76
	phisrc.32   %phi26 <- %r77
	br          .L34

.L34:
	phi.32      %r78 <- %phi25, %phi26
	phisrc.32   %phi27(return) <- %r78
	br          .L31

.L31:
	phi.32      %r79 <- %phi27(return)
	ret.32      %r79


ad:
.L35:
	<entry-point>
	store.32    %arg1 -> 0[i]
	store.64    %arg2 -> 0[b]
	load.32     %r80 <- 0[i]
	setne.1     %r81 <- %r80, $0
	cbr         %r81, .L37, .L38

.L37:
	load.64     %r82 <- 0[b]
	load.64     %r83 <- 16[%r82]
	setfval.64  %r84 <- 0.000000e+00
	fcmpune.1   %r85 <- %r83, %r84
	zext.32     %r86 <- (1) %r85
	phisrc.32   %phi29 <- %r86
	br          .L39

.L38:
	setne.1     %r87 <- $0, $0
	zext.32     %r88 <- (1) %r87
	phisrc.32   %phi30 <- %r88
	br          .L39

.L39:
	phi.32      %r89 <- %phi29, %phi30
	phisrc.32   %phi31(return) <- %r89
	br          .L36

.L36:
	phi.32      %r90 <- %phi31(return)
	ret.32      %r90


 * check-output-end
 */
