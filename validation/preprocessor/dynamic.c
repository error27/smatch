#if defined(__LINE__)
__LINE__
#endif
#if defined(__FILE__)
__FILE__
#endif
#if defined(__DATE__)
date
#endif
#if defined(__TIME__)
time
#endif
#if defined(__COUNTER__)
counter
#endif

/*
 * check-name: dynamic-macros
 * check-command: sparse -E $file
 * check-known-to-fail
 *
 * check-output-start

2
"preprocessor/dynamic.c"
date
time
counter
 * check-output-end
 */
