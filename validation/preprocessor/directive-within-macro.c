#define f(x) x

f(1
#if 1		// OK
  a
#elif 2		// OK
  b
#else		// OK
  c
#endif		// OK
#ifdef f	// OK
  d
#endif		// OK
#ifndef f	// OK
  e
#endif		// OK
  3)

f(1
#define x y	// KO
  3)

/*
 * check-name: directive-within-macro
 * check-command: sparse -E $file
 *
 * check-output-start

1 a d 3
1 3
 * check-output-end
 *
 * check-error-start
preprocessor/directive-within-macro.c:20:1: warning: directive in macro's argument list
 * check-error-end
 */
