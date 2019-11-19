#define __pure		__attribute__((pure))
#define __noreturn	__attribute__((noreturn))


int __pure	p(int i);
int		p(int i) { return i; }

void __noreturn	n(void);
void		n(void) { while (1) ; }

/*
 * check-name: function-redecl-funattr
 */
