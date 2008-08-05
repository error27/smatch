static int x = __builtin_choose_expr(0,(char *)0,(void)0);
static int y = __builtin_choose_expr(1,(char *)0,(void)0);
static char s[42];
static int z = 1/(sizeof(__builtin_choose_expr(1,s,0)) - 42);
