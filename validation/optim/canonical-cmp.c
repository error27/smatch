typedef	  signed int	sint;
typedef	unsigned int	uint;

sint seq(sint p, sint a) { return (123 == p) ? a : 0; }
sint sne(sint p, sint a) { return (123 != p) ? a : 0; }

sint slt(sint p, sint a) { return (123 >  p) ? a : 0; }
sint sle(sint p, sint a) { return (123 >= p) ? a : 0; }
sint sge(sint p, sint a) { return (123 <= p) ? a : 0; }
sint sgt(sint p, sint a) { return (123 <  p) ? a : 0; }

uint ueq(uint p, uint a) { return (123 == p) ? a : 0; }
uint une(uint p, uint a) { return (123 != p) ? a : 0; }

uint ubt(uint p, uint a) { return (123 >  p) ? a : 0; }
uint ube(uint p, uint a) { return (123 >= p) ? a : 0; }
uint uae(uint p, uint a) { return (123 <= p) ? a : 0; }
uint uat(uint p, uint a) { return (123 <  p) ? a : 0; }

/*
 * check-name: canonical-cmp
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-excludes: \\$123,
 *
 * check-output-start
seq:
.L0:
	<entry-point>
	seteq.32    %r4 <- %arg1, $123
	select.32   %r5 <- %r4, %arg2, $0
	ret.32      %r5


sne:
.L2:
	<entry-point>
	setne.32    %r11 <- %arg1, $123
	select.32   %r12 <- %r11, %arg2, $0
	ret.32      %r12


slt:
.L4:
	<entry-point>
	setlt.32    %r18 <- %arg1, $123
	select.32   %r19 <- %r18, %arg2, $0
	ret.32      %r19


sle:
.L6:
	<entry-point>
	setle.32    %r25 <- %arg1, $123
	select.32   %r26 <- %r25, %arg2, $0
	ret.32      %r26


sge:
.L8:
	<entry-point>
	setge.32    %r32 <- %arg1, $123
	select.32   %r33 <- %r32, %arg2, $0
	ret.32      %r33


sgt:
.L10:
	<entry-point>
	setgt.32    %r39 <- %arg1, $123
	select.32   %r40 <- %r39, %arg2, $0
	ret.32      %r40


ueq:
.L12:
	<entry-point>
	seteq.32    %r45 <- %arg1, $123
	select.32   %r46 <- %r45, %arg2, $0
	ret.32      %r46


une:
.L14:
	<entry-point>
	setne.32    %r50 <- %arg1, $123
	select.32   %r51 <- %r50, %arg2, $0
	ret.32      %r51


ubt:
.L16:
	<entry-point>
	setb.32     %r55 <- %arg1, $123
	select.32   %r56 <- %r55, %arg2, $0
	ret.32      %r56


ube:
.L18:
	<entry-point>
	setbe.32    %r60 <- %arg1, $123
	select.32   %r61 <- %r60, %arg2, $0
	ret.32      %r61


uae:
.L20:
	<entry-point>
	setae.32    %r65 <- %arg1, $123
	select.32   %r66 <- %r65, %arg2, $0
	ret.32      %r66


uat:
.L22:
	<entry-point>
	seta.32     %r70 <- %arg1, $123
	select.32   %r71 <- %r70, %arg2, $0
	ret.32      %r71


 * check-output-end
 */
