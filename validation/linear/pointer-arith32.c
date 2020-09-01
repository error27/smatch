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

char *cpq(char *data, long long pos)
{
	data += pos;
	return data;
}

int *ipq_ref(int *data, long long pos)
{
	data = data + pos;
	return data;
}

int *ipq(int *data, long long pos)
{
	data += pos;
	return data;
}

/*
 * check-name: pointer-arith32
 * check-command: test-linearize -Wno-decl --arch=generic -m32 $file
 *
 * check-output-start
cps:
.L0:
	<entry-point>
	sext.32     %r2 <- (16) %arg2
	add.32      %r5 <- %arg1, %r2
	ret.32      %r5


ipss:
.L2:
	<entry-point>
	sext.32     %r10 <- (16) %arg2
	mul.32      %r11 <- %r10, $4
	add.32      %r14 <- %arg1, %r11
	ret.32      %r14


ipus:
.L4:
	<entry-point>
	zext.32     %r19 <- (16) %arg2
	mul.32      %r20 <- %r19, $4
	add.32      %r23 <- %arg1, %r20
	ret.32      %r23


cpq:
.L6:
	<entry-point>
	trunc.32    %r28 <- (64) %arg2
	add.32      %r31 <- %arg1, %r28
	ret.32      %r31


ipq_ref:
.L8:
	<entry-point>
	trunc.32    %r37 <- (64) %arg2
	mul.32      %r38 <- %r37, $4
	add.32      %r39 <- %arg1, %r38
	ret.32      %r39


ipq:
.L10:
	<entry-point>
	trunc.32    %r43 <- (64) %arg2
	mul.32      %r44 <- %r43, $4
	add.32      %r47 <- %arg1, %r44
	ret.32      %r47


 * check-output-end
 */
