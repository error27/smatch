/*
 * Should result in
 * #define X 1
 * X
 * since only # in the input stream marks beginning of preprocessor command
 * and here we get it from macro expansion. 
 */
#define A # define X 1
A
X
