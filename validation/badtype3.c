int
foo (int (*func) (undef, void *), void *data)
{
  int err = 0;
  while (cur) {
    if ((*func) (cur, data))
      break;
  }
  return err;
}
