struct s {
	int:16;
	short f:6;
};

static short local(struct s s)
{
	return s.f;
}

static void foo(struct s s)
{
	while (s.f) ;
}

/*
 * check-name: not-same-memop0
 * check-command: test-linearize -Wno-decl -fdump-ir=mem2reg $file
 *
 * check-output-start
local:
.L0:
	<entry-point>
	store.32    %arg1 -> 0[s]
	load.16     %r1 <- 2[s]
	trunc.6     %r2 <- (16) %r1
	sext.16     %r3 <- (6) %r2
	ret.16      %r3


foo:
.L2:
	<entry-point>
	store.32    %arg1 -> 0[s]
	br          .L6

.L6:
	load.16     %r5 <- 2[s]
	trunc.6     %r6 <- (16) %r5
	setne.1     %r7 <- %r6, $0
	cbr         %r7, .L6, .L5

.L5:
	ret


 * check-output-end
 */
