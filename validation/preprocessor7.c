#define A(x) C(B, D
#define D A(1))
#define C(x,y) E(y)
#define E(y) #y
A(2))
