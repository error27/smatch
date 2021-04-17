int f00(int x) { return x >= 0; }
int f01(int x) { return x > -1; }
int f02(int x) { return x <  1; }
int f03(int x) { return x <= 0; }

int f10(int x) { return x <  16; }
int f11(int x) { return x <= 15; }

int f20(int x) { return x >  -9; }
int f21(int x) { return x >= -8; }

/*
 * check-name: canonical-cmp-zero
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
f00:
.L0:
	<entry-point>
	setge.32    %r2 <- %arg1, $0
	ret.32      %r2


f01:
.L2:
	<entry-point>
	setge.32    %r5 <- %arg1, $0
	ret.32      %r5


f02:
.L4:
	<entry-point>
	setle.32    %r8 <- %arg1, $0
	ret.32      %r8


f03:
.L6:
	<entry-point>
	setle.32    %r11 <- %arg1, $0
	ret.32      %r11


f10:
.L8:
	<entry-point>
	setle.32    %r14 <- %arg1, $15
	ret.32      %r14


f11:
.L10:
	<entry-point>
	setle.32    %r17 <- %arg1, $15
	ret.32      %r17


f20:
.L12:
	<entry-point>
	setge.32    %r20 <- %arg1, $0xfffffff8
	ret.32      %r20


f21:
.L14:
	<entry-point>
	setge.32    %r23 <- %arg1, $0xfffffff8
	ret.32      %r23


 * check-output-end
 */
