extern int foo (const char *, ...);
void sparse_error(const char err[])
{
  foo("%s\n",err);
}
