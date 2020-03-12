#define f(x) x x
f(1
#undef  f
#define f 2
  f)

/*
 * check-name: expand-redef
 * check-command: sparse -E $file
 *
 * check-output-start

1 2 1 2
 * check-output-end
 *
 * check-error-start
preprocessor/expand-redef.c:3:1: warning: directive in macro's argument list
preprocessor/expand-redef.c:4:1: warning: directive in macro's argument list
 * check-error-end
 */
