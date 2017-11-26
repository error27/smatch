extern int fun(void);

void fa(void) { int (*f)(void); f = &fun; }
void f0(void) { int (*f)(void); f = fun; }	// C99,C11 6.3.2.1p4

/*
 * check-name: type of function pointers
 * check-command: sparse -Wno-decl $file
 */
