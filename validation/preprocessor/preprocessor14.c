/*
 * GNU kludge, another corner case
 */
#define A(x,y,...) ,##x##__VA_ARGS__
A(,1)
#define B(x,y,...) x##,##__VA_ARGS__
B(,1)
