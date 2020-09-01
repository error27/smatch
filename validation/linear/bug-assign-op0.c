int asr(int s)
{
	s >>= 11U;
	return s;
}

unsigned int lsr(unsigned int u)
{
	u >>= 11;
	return u;
}

int divr(int s, unsigned long long u)
{
	extern int use(int, unsigned);
	int t = s;
	s = s / u;
	u = u / t;
	return use(s, u);
}

int sdivul(int s, unsigned long long u)
{
	s /= u;			// divu
	return s;
}

unsigned int udivsl(unsigned int u, long long s)
{
	u /= s;			// divs
	return u;
}

int uldivs(int s, unsigned long long u)
{
	u /= s;			// divu
	return u;
}

unsigned int sldivu(unsigned int u, long long s)
{
	s /= u;			// divs
	return s;
}

/*
 * check-name: bug-assign-op0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
asr:
.L0:
	<entry-point>
	asr.32      %r2 <- %arg1, $11
	ret.32      %r2


lsr:
.L2:
	<entry-point>
	lsr.32      %r6 <- %arg1, $11
	ret.32      %r6


divr:
.L4:
	<entry-point>
	sext.64     %r11 <- (32) %arg1
	divu.64     %r13 <- %r11, %arg2
	trunc.32    %r14 <- (64) %r13
	divu.64     %r18 <- %arg2, %r11
	trunc.32    %r21 <- (64) %r18
	call.32     %r22 <- use, %r14, %r21
	ret.32      %r22


sdivul:
.L6:
	<entry-point>
	sext.64     %r26 <- (32) %arg1
	divu.64     %r27 <- %r26, %arg2
	trunc.32    %r28 <- (64) %r27
	ret.32      %r28


udivsl:
.L8:
	<entry-point>
	zext.64     %r33 <- (32) %arg1
	divs.64     %r34 <- %r33, %arg2
	trunc.32    %r35 <- (64) %r34
	ret.32      %r35


uldivs:
.L10:
	<entry-point>
	sext.64     %r39 <- (32) %arg1
	divu.64     %r41 <- %arg2, %r39
	trunc.32    %r43 <- (64) %r41
	ret.32      %r43


sldivu:
.L12:
	<entry-point>
	zext.64     %r46 <- (32) %arg1
	divs.64     %r48 <- %arg2, %r46
	trunc.32    %r50 <- (64) %r48
	ret.32      %r50


 * check-output-end
 */
