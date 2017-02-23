int and_0(int a)
{
	return a && 0;
}

int and_1(int a)
{
	return a && 1;
}

int or_0(int a)
{
	return a || 0;
}

int or_1(int a)
{
	return a || 1;
}

// try again but with something true but != 1
int and_2(int a)
{
	return a && 2;
}

int or_2(int a)
{
	return a || 2;
}

/*
 * check-name: bool-simplify
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
and_0:
.L0:
	<entry-point>
	ret.32      $0


and_1:
.L2:
	<entry-point>
	setne.1     %r8 <- %arg1, $0
	zext.32     %r11 <- (1) %r8
	ret.32      %r11


or_0:
.L4:
	<entry-point>
	setne.1     %r14 <- %arg1, $0
	zext.32     %r17 <- (1) %r14
	ret.32      %r17


or_1:
.L6:
	<entry-point>
	ret.32      $1


and_2:
.L8:
	<entry-point>
	setne.1     %r26 <- %arg1, $0
	zext.32     %r29 <- (1) %r26
	ret.32      %r29


or_2:
.L10:
	<entry-point>
	ret.32      $1


 * check-output-end
 */
