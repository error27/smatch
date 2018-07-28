struct u {
	unsigned int f:3;
};

unsigned int upostinc(struct u *x)
{
	return x->f++;
}

unsigned int upreinc(struct u *x)
{
	return ++x->f;
}

void ucpy(struct u *d, const struct u *s)
{
	d->f = s->f;
}


struct s {
	int f:3;
};

int spostinc(struct s *x)
{
	return x->f++;
}

int spreinc(struct s *x)
{
	return ++x->f;
}

void scpy(struct s *d, const struct s *s)
{
	d->f = s->f;
}

/*
 * check-name: bitfield-size
 * check-command: test-linearize -m64 -Wno-decl -fdump-ir  $file
 *
 * check-output-start
upostinc:
.L0:
	<entry-point>
	store.64    %arg1 -> 0[x]
	load.64     %r1 <- 0[x]
	load.32     %r2 <- 0[%r1]
	trunc.3     %r3 <- (32) %r2
	add.3       %r4 <- %r3, $1
	load.32     %r5 <- 0[%r1]
	zext.32     %r6 <- (3) %r4
	and.32      %r7 <- %r5, $0xfffffff8
	or.32       %r8 <- %r7, %r6
	store.32    %r8 -> 0[%r1]
	zext.32     %r9 <- (3) %r3
	phisrc.32   %phi1(return) <- %r9
	br          .L1

.L1:
	phi.32      %r10 <- %phi1(return)
	ret.32      %r10


upreinc:
.L2:
	<entry-point>
	store.64    %arg1 -> 0[x]
	load.64     %r11 <- 0[x]
	load.32     %r12 <- 0[%r11]
	trunc.3     %r13 <- (32) %r12
	add.3       %r14 <- %r13, $1
	load.32     %r15 <- 0[%r11]
	zext.32     %r16 <- (3) %r14
	and.32      %r17 <- %r15, $0xfffffff8
	or.32       %r18 <- %r17, %r16
	store.32    %r18 -> 0[%r11]
	zext.32     %r19 <- (3) %r14
	phisrc.32   %phi2(return) <- %r19
	br          .L3

.L3:
	phi.32      %r20 <- %phi2(return)
	ret.32      %r20


ucpy:
.L4:
	<entry-point>
	store.64    %arg1 -> 0[d]
	store.64    %arg2 -> 0[s]
	load.64     %r21 <- 0[s]
	load.32     %r22 <- 0[%r21]
	trunc.3     %r23 <- (32) %r22
	load.64     %r24 <- 0[d]
	load.32     %r25 <- 0[%r24]
	zext.32     %r26 <- (3) %r23
	and.32      %r27 <- %r25, $0xfffffff8
	or.32       %r28 <- %r27, %r26
	store.32    %r28 -> 0[%r24]
	br          .L5

.L5:
	ret


spostinc:
.L6:
	<entry-point>
	store.64    %arg1 -> 0[x]
	load.64     %r29 <- 0[x]
	load.32     %r30 <- 0[%r29]
	trunc.3     %r31 <- (32) %r30
	add.3       %r32 <- %r31, $1
	load.32     %r33 <- 0[%r29]
	zext.32     %r34 <- (3) %r32
	and.32      %r35 <- %r33, $0xfffffff8
	or.32       %r36 <- %r35, %r34
	store.32    %r36 -> 0[%r29]
	zext.32     %r37 <- (3) %r31
	phisrc.32   %phi3(return) <- %r37
	br          .L7

.L7:
	phi.32      %r38 <- %phi3(return)
	ret.32      %r38


spreinc:
.L8:
	<entry-point>
	store.64    %arg1 -> 0[x]
	load.64     %r39 <- 0[x]
	load.32     %r40 <- 0[%r39]
	trunc.3     %r41 <- (32) %r40
	add.3       %r42 <- %r41, $1
	load.32     %r43 <- 0[%r39]
	zext.32     %r44 <- (3) %r42
	and.32      %r45 <- %r43, $0xfffffff8
	or.32       %r46 <- %r45, %r44
	store.32    %r46 -> 0[%r39]
	zext.32     %r47 <- (3) %r42
	phisrc.32   %phi4(return) <- %r47
	br          .L9

.L9:
	phi.32      %r48 <- %phi4(return)
	ret.32      %r48


scpy:
.L10:
	<entry-point>
	store.64    %arg1 -> 0[d]
	store.64    %arg2 -> 0[s]
	load.64     %r49 <- 0[s]
	load.32     %r50 <- 0[%r49]
	trunc.3     %r51 <- (32) %r50
	load.64     %r52 <- 0[d]
	load.32     %r53 <- 0[%r52]
	zext.32     %r54 <- (3) %r51
	and.32      %r55 <- %r53, $0xfffffff8
	or.32       %r56 <- %r55, %r54
	store.32    %r56 -> 0[%r52]
	br          .L11

.L11:
	ret


 * check-output-end
 */
