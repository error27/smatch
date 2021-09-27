static void asm_out0(void)
{
	int mem;
	asm volatile ("[%1] <= 0" : "=m" (mem));
}

/*
 * check-name: asm-out0
 * check-command: test-linearize -m64 -fdump-ir $file
 *
 * check-output-start
asm_out0:
.L0:
	<entry-point>
	symaddr.64  %r1 <- mem
	asm         "[%1] <= 0"
		out: "=m" (%r1)
	br          .L1

.L1:
	ret


 * check-output-end
 */
