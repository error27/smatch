struct s {
	int:16;
	int f:16;
} __attribute__((__packed__));

static void foo(struct s s)
{
	while (s.f)
		;
}

/*
 * check-name: packed-bitfield
 * check-command: test-linearize -fmem2reg $file
 *
 * check-output-contains: store.32
 * check-output-contains: load.16
 *
 * check-output-start
foo:
.L0:
	<entry-point>
	store.32    %arg1 -> 0[s]
	br          .L4

.L4:
	load.16     %r1 <- 2[s]
	cbr         %r1, .L4, .L3

.L3:
	ret


 * check-output-end
 */
