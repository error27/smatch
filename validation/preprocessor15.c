#define A defi
#define B ned
#define C(x,y) x##y
#define D(x,y) C(x,y)
#if D(A,B) B
D(1,2)
#endif
