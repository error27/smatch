extern int foo (const char *, ...);
void error(const char err[])
{
  foo("%s\n",err);
}
