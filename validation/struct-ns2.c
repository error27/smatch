static void
g (struct Bar { int i; } *x)
{
  struct Bar y;
  y.i = 1;
}

static void
h (void)
{
  // This is not in scope and should barf loudly.
  struct Bar y;
  y.i = 1;
}
