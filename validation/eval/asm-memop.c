extern int g;

void fo0(int *p) { asm volatile ("op %0" :: "p" (&g)); }
void fo1(int *p) { asm volatile ("op %0" :: "m" (g)); }

void fo2(int *p) { asm volatile ("op %0" :: "p" (p)); }
void fo3(int *p) { asm volatile ("op %0" :: "m" (*p)); }

/*
 * check-name: eval-asm-memop
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
fo0:
.L0:
	<entry-point>
	asm         "op %0"
		in: "p" (g)
	ret


fo1:
.L2:
	<entry-point>
	asm         "op %0"
		in: "m" (g)
	ret


fo2:
.L4:
	<entry-point>
	asm         "op %0"
		in: "p" (%arg1)
	ret


fo3:
.L6:
	<entry-point>
	asm         "op %0"
		in: "m" (%arg1)
	ret


 * check-output-end
 */
