#define	bool	_Bool

bool bfimp(float a) { return a; }
bool bfexp(float a) { return (bool)a; }

bool bfnot(float a) { return !a; }
int  ifnot(float a) { return !a; }

/*
 * check-name: bool context fp
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
bfimp:
.L0:
	<entry-point>
	setfval.32  %r2 <- 0.000000
	fcmpune.1   %r3 <- %arg1, %r2
	ret.1       %r3


bfexp:
.L2:
	<entry-point>
	setfval.32  %r6 <- 0.000000
	fcmpune.1   %r7 <- %arg1, %r6
	ret.1       %r7


bfnot:
.L4:
	<entry-point>
	setfval.32  %r10 <- 0.000000
	fcmpoeq.1   %r12 <- %arg1, %r10
	ret.1       %r12


ifnot:
.L6:
	<entry-point>
	setfval.32  %r15 <- 0.000000
	fcmpoeq.32  %r16 <- %arg1, %r15
	ret.32      %r16


 * check-output-end
 */
