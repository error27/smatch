int cmpint(   int x,   int y)	{ return x == y; }
int cmpflt( float x, float y)	{ return x == y; }
int cmpvptr(void *x, void *y)	{ return x == y; }
int cmpiptr(int  *x, int  *y)	{ return x == y; }

/*
 * check-name: pointer comparison
 * check-command: ./sparsec -Wno-decl -c $file -o tmp.o
 */
