//typedef int undef;
extern undef bar(void);
static undef foo(char *c)
{
  char p = *c;
  switch (p) {
  default:
    return bar();
  }
}
