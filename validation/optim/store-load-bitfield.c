int ufoo(unsigned int a)
{
	struct u {
		unsigned int :2;
		unsigned int a:3;
	} bf;

	bf.a = a;
	return bf.a;
}

int sfoo(int a)
{
	struct s {
		signed int :2;
		signed int a:3;
	} bf;

	bf.a = a;
	return bf.a;
}

/*
 * check-name: optim store/load bitfields
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
ufoo:
.L0:
	<entry-point>
	and.32      %r4 <- %arg1, $7
	shl.32      %r5 <- %r4, $2
	lsr.32      %r9 <- %r5, $2
	and.32      %r11 <- %r9, $7
	ret.32      %r11


sfoo:
.L2:
	<entry-point>
	and.32      %r16 <- %arg1, $7
	shl.32      %r17 <- %r16, $2
	lsr.32      %r21 <- %r17, $2
	trunc.3     %r22 <- (32) %r21
	sext.32     %r23 <- (3) %r22
	ret.32      %r23


 * check-output-end
 */
