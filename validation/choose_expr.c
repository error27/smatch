int x = __builtin_choose_expr(0,(char *)0,(void)0);
int y = __builtin_choose_expr(1,(char *)0,(void)0);
char s[42];
int z = 1/(sizeof(__builtin_choose_expr(1,s,0)) - 42);
