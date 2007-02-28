#define MY_MACRO(a) do { \
  __builtin_warning(!__builtin_safe_p(a), "Macro argument with side effects"); \
    a;	\
  } while (0)

int g(int);
int h(int) __attribute__((pure));
int i(int) __attribute__((const));

static int foo(int x, int y)
{
  /* unsafe: */
  MY_MACRO(x++);
  MY_MACRO(x+=1);
  MY_MACRO(x=x+1);
  MY_MACRO(x%=y);
  MY_MACRO(x=y);
  MY_MACRO(g(x));
  MY_MACRO((y,g(x)));
  /* safe: */
  MY_MACRO(x+1);
  MY_MACRO(h(x));
  MY_MACRO(i(x));
  return x;
}

