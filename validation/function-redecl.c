#define __user	__attribute__((address_space(1)))
#define NULL	((void*)0)

int ret_type(void);
void ret_type(void) { }				/* check-should-fail */


int ret_const(void);
int const ret_const(void) { return 0; }		/* check-should-fail */


void *ret_as(void);
void __user *ret_as(void) { return NULL; }	/* check-should-fail */


void *ret_mod(void);
void const *ret_mod(void) { return NULL; }	/* check-should-fail */


void arg_type(int a);
void arg_type(void *a) { }			/* check-should-fail */


void arg_const(int a);
void arg_const(const int a) { }			/* OK */


void arg_as(void *a);
void arg_as(void __user *a) { }			/* check-should-fail */


void arg_mod(void *);
void arg_mod(void const *a) { }			/* check-should-fail */


void arg_more_arg(int a);
void arg_more_arg(int a, int b) { }		/* check-should-fail */


void arg_less_arg(int a, int b);
void arg_less_arg(int a) { }			/* check-should-fail */


void arg_vararg(int a);
void arg_vararg(int a, ...) { }			/* check-should-fail */

/*
 * check-name: function-redecl
 *
 * check-error-start
function-redecl.c:5:6: error: symbol 'ret_type' redeclared with different type (different base types):
function-redecl.c:5:6:    void extern [addressable] [toplevel] ret_type( ... )
function-redecl.c:4:5: note: previously declared as:
function-redecl.c:4:5:    int extern [addressable] [signed] [toplevel] ret_type( ... )
function-redecl.c:9:11: error: symbol 'ret_const' redeclared with different type (different modifiers):
function-redecl.c:9:11:    int extern const [addressable] [signed] [toplevel] ret_const( ... )
function-redecl.c:8:5: note: previously declared as:
function-redecl.c:8:5:    int extern [addressable] [signed] [toplevel] ret_const( ... )
function-redecl.c:13:13: error: symbol 'ret_as' redeclared with different type (different address spaces):
function-redecl.c:13:13:    void <asn:1> *extern [addressable] [toplevel] ret_as( ... )
function-redecl.c:12:6: note: previously declared as:
function-redecl.c:12:6:    void *extern [addressable] [toplevel] ret_as( ... )
function-redecl.c:17:12: error: symbol 'ret_mod' redeclared with different type (different modifiers):
function-redecl.c:17:12:    void const *extern [addressable] [toplevel] ret_mod( ... )
function-redecl.c:16:6: note: previously declared as:
function-redecl.c:16:6:    void *extern [addressable] [toplevel] ret_mod( ... )
function-redecl.c:21:6: error: symbol 'arg_type' redeclared with different type (incompatible argument 1 (different base types)):
function-redecl.c:21:6:    void extern [addressable] [toplevel] arg_type( ... )
function-redecl.c:20:6: note: previously declared as:
function-redecl.c:20:6:    void extern [addressable] [toplevel] arg_type( ... )
function-redecl.c:29:6: error: symbol 'arg_as' redeclared with different type (incompatible argument 1 (different address spaces)):
function-redecl.c:29:6:    void extern [addressable] [toplevel] arg_as( ... )
function-redecl.c:28:6: note: previously declared as:
function-redecl.c:28:6:    void extern [addressable] [toplevel] arg_as( ... )
function-redecl.c:33:6: error: symbol 'arg_mod' redeclared with different type (incompatible argument 1 (different modifiers)):
function-redecl.c:33:6:    void extern [addressable] [toplevel] arg_mod( ... )
function-redecl.c:32:6: note: previously declared as:
function-redecl.c:32:6:    void extern [addressable] [toplevel] arg_mod( ... )
function-redecl.c:37:6: error: symbol 'arg_more_arg' redeclared with different type (different argument counts):
function-redecl.c:37:6:    void extern [addressable] [toplevel] arg_more_arg( ... )
function-redecl.c:36:6: note: previously declared as:
function-redecl.c:36:6:    void extern [addressable] [toplevel] arg_more_arg( ... )
function-redecl.c:41:6: error: symbol 'arg_less_arg' redeclared with different type (different argument counts):
function-redecl.c:41:6:    void extern [addressable] [toplevel] arg_less_arg( ... )
function-redecl.c:40:6: note: previously declared as:
function-redecl.c:40:6:    void extern [addressable] [toplevel] arg_less_arg( ... )
function-redecl.c:45:6: error: symbol 'arg_vararg' redeclared with different type (incompatible variadic arguments):
function-redecl.c:45:6:    void extern [addressable] [toplevel] arg_vararg( ... )
function-redecl.c:44:6: note: previously declared as:
function-redecl.c:44:6:    void extern [addressable] [toplevel] arg_vararg( ... )
 * check-error-end
 */
