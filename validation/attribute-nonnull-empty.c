/*
 * An empty attribute argument list, like __attribute__((nonnull())), is
 * accepted by GCC and Clang (equivalent to the bare __attribute__((nonnull)))
 * and must be parsed by sparse too.
 */
extern void *bare (void *d, const void *s) __attribute__((nonnull));
extern void *empty(void *d, const void *s) __attribute__((nonnull()));
extern void *args (void *d, const void *s) __attribute__((nonnull(1, 2)));

/*
 * check-name: attribute with empty argument list
 */
