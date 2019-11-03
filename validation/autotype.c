#ifdef __CHECKER__
#define	is_type(X, T)	_Static_assert([typeof(X)] == [T], "")
#else
#define	is_type(X, T)	_Static_assert(1, "")
#endif

struct s {
	int x;
	int bf:3;
};

extern char ch;
extern const int ci;

__auto_type i = 0;		is_type(i, int);
__auto_type m = 1UL;		is_type(m, unsigned long);
__auto_type l = (int)0L;	is_type(l, int);
__auto_type c = (char)'\n';	is_type(c, char);
__auto_type p = &i;		is_type(p, int *);
__auto_type f = 0.0;		is_type(f, double);
__auto_type s = (struct s){0};	is_type(s, struct s);
__auto_type pci = &ci;		is_type(pci, const int *);

// ~~: not valid for bitfield
__auto_type b = (struct s){0}.bf; is_type(b, int);

static __auto_type si = 0;	is_type(si, int);
const  __auto_type ci = 0;	is_type(ci, const int);
__auto_type ch = (char) '\n';	is_type(ch, char);

static int foo(int a)
{
	__auto_type i = a;	is_type(i, int);
	__auto_type c = ch;	is_type(c, char);
	__auto_type ct = ci;	is_type(&ct, const int *);

	return ct += i + c;
}



#define __as __attribute__((address_space(42)))
extern int __as aa;

__auto_type pa = &aa;		is_type(pa, int __as *);

/*
 * check-name: autotype
 * check-command: sparse -Wno-decl $file
 *
 * check-error-start
autotype.c:25:13: warning: __auto_type on bitfield
autotype.c:37:16: error: assignment to const expression
 * check-error-end
 */
