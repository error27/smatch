#if 1
#if
/*
 * check-name: Preprocessor #21
 * check-description: This used to hang Sparse.
 * check-command: sparse -E $file
 * check-exit-value: 1
 *
 * check-output-start
preprocessor/preprocessor21.c:2:2: error: unterminated preprocessor conditional


 * check-output-end
 *
 * check-known-to-fail
 */
