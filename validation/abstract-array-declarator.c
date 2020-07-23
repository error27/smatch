void f77(int a[1, 2]);
void c99(int a[(1, 2)]);

/*
 * check-name: abstract-array-declarator
 *
 * check-error-start
abstract-array-declarator.c:1:17: error: Expected ] in abstract_array_declarator
abstract-array-declarator.c:1:17: error: got ,
 * check-error-end
 */
