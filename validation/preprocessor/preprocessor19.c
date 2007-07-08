/* got burned by that - freed the new defintion in the case when we had
   warned and replaced the old one */
#define A x
#define A y
A
/*
 * check-name: Preprocessor #19
 * check-command: sparse -E $file
 *
 * check-output-start
preprocessor/preprocessor19.c:4:9: warning: preprocessor token A redefined
preprocessor/preprocessor19.c:3:9: this was the original definition

y
 * check-output-end
 */
