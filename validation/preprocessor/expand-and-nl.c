#define M(X) X-X

M(a
b)
/*
 * check-name: expand-and-nl
 * check-command: sparse -E $file
 *
 * check-output-start

a b-a b
 * check-output-end
 */
