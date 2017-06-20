#if defined(__LITTLE_ENDIAN__)
#error "__LITTLE_ENDIAN__ defined!"
#endif
#if (__BIG_ENDIAN__ != 1)
#error "__BIG_ENDIAN__ not correctly defined!"
#endif

/*
 * check-name: endian-big.c
 * check-command: sparse -mbig-endian $file
 */
