extern long l;
extern unsigned int u;

#if __SIZEOF_LONG__ == __SIZEOF_INT__
_Static_assert([typeof(l + u)] == [unsigned long], "ulong");
#endif

/*
 * check-name: usual-conversions
 * check-command: sparse -m32 $file
 */
