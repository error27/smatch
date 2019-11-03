__auto_type u;				// KO: no initializer
__auto_type r[2] = { 0, 1 };		// KO: not a plain identifier
__auto_type foo(void) { }		// KO: not a plain identifier
__auto_type v = 0, w = 1;		// KO: in list
struct { __auto_type x; } s;		// KO: not valid for struct/union
__auto_type self = self;		// KO: self-declared
__auto_type undc = this;		// KO: undeclared

int i = 1;
double f = 1.0;
__auto_type i = 2;			// KO: redecl, same type
__auto_type f = 2.0f;			// KO: redecl, diff type


static int foo(int a, const int *ptr)
{
	__auto_type i = a;
	__auto_type c = *ptr;

	c += 1;
	return i;
}

/*
 * check-name: autotype-ko
 * check-command: sparse -Wno-decl $file
 *
 * check-error-start
autotype-ko.c:1:13: error: __auto_type without initializer
autotype-ko.c:2:13: error: __auto_type on non-identifier
autotype-ko.c:3:13: error: 'foo()' has __auto_type return type
autotype-ko.c:4:13: error: __auto_type on declaration list
autotype-ko.c:6:13: error: __auto_type on self-init var
autotype-ko.c:2:20: error: invalid initializer
autotype-ko.c:5:22: error: member 'x' has __auto_type
autotype-ko.c:7:20: error: undefined identifier 'this'
autotype-ko.c:11:13: error: symbol 'i' has multiple initializers (originally initialized at autotype-ko.c:9)
autotype-ko.c:12:13: error: symbol 'f' has multiple initializers (originally initialized at autotype-ko.c:10)
autotype-ko.c:12:13: error: symbol 'f' redeclared with different type (different type sizes):
autotype-ko.c:12:13:    float [addressable] [toplevel] f
autotype-ko.c:10:8: note: previously declared as:
autotype-ko.c:10:8:    double [addressable] [toplevel] f
autotype-ko.c:20:9: error: assignment to const expression
 * check-error-end
 */
