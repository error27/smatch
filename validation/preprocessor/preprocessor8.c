#define A(x) ## x
#define B(x) x ##
#define C(x) x ## ## ##
#define D(x) x#y
#define E x#y
#define F(x,y) x x##y #x y
#define G a##b
#define H 1##2
#define I(x,y,z) x y z
"A(x)"			: A(x)
"B(x)"			: B(x)
"C(x)"			: C(x)
"D(x)"			: D(x)
"x#y"			: E
"ab GH \"G\" 12"	: F(G,H)
"a ## b"		: I(a,##,b)
