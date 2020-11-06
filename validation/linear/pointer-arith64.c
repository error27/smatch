char *cps(char *data, short pos)
{
	data += pos;
	return data;
}

int *ipss(int *data, short pos)
{
	data += pos;
	return data;
}
int *ipus(int *data, unsigned short pos)
{
	data += pos;
	return data;
}

int *ipsi(int *data, int pos)
{
	data += pos;
	return data;
}
int *ipui(int *data, unsigned int pos)
{
	data += pos;
	return data;
}

/*
 * check-name: pointer-arith64
 * check-command: test-linearize -Wno-decl --arch=generic -m64 $file
 *
 * check-output-start
cps:
.L0:
	<entry-point>
	sext.64     %r2 <- (16) %arg2
	add.64      %r5 <- %r2, %arg1
	ret.64      %r5


ipss:
.L2:
	<entry-point>
	sext.64     %r10 <- (16) %arg2
	mul.64      %r11 <- %r10, $4
	add.64      %r14 <- %r11, %arg1
	ret.64      %r14


ipus:
.L4:
	<entry-point>
	zext.64     %r19 <- (16) %arg2
	mul.64      %r20 <- %r19, $4
	add.64      %r23 <- %r20, %arg1
	ret.64      %r23


ipsi:
.L6:
	<entry-point>
	sext.64     %r28 <- (32) %arg2
	mul.64      %r29 <- %r28, $4
	add.64      %r32 <- %r29, %arg1
	ret.64      %r32


ipui:
.L8:
	<entry-point>
	zext.64     %r37 <- (32) %arg2
	mul.64      %r38 <- %r37, $4
	add.64      %r41 <- %r38, %arg1
	ret.64      %r41


 * check-output-end
 */
