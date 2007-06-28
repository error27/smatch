/*
 * GNU kludge, corner case
 */
#define A(x,...) x##,##__VA_ARGS__
A(1)
A(1,2)
A(1,2,3)
