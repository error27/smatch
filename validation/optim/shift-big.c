typedef unsigned int u32;
typedef          int s32;

static u32 lsr32(u32 a) { return a >> 32; }
static s32 asr32(s32 a) { return a >> 32; }
static u32 shl32(u32 a) { return a << 32; }

/*
 * check-name: optim/shift-big.c
 * check-command: test-linearize -fnormalize-pseudos $file
 *
 * check-error-ignore
 * check-output-start
lsr32:
.L0:
	<entry-point>
	ret.32      $0


asr32:
.L2:
	<entry-point>
	asr.32      %r5 <- %arg1, $32
	ret.32      %r5


shl32:
.L4:
	<entry-point>
	ret.32      $0


 * check-output-end
 */
