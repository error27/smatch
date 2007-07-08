#if 0
/* these should not warn */
#ifdef (
#endif
#ifndef (
#endif
#endif
/*
 * check-name: Preprocessor #17
 *
 * check-command: sparse -E $file
 * check-exit-value: 0
 * check-output-start


 * check-output-end
 */
