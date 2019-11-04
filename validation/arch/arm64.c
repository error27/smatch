__aarch64__
__x86_64__
__LP64__
__BYTE_ORDER__
__SIZEOF_INT__
__SIZEOF_LONG__
__SIZE_TYPE__

/*
 * check-name: arch/arm64
 * check-command: sparse --arch=arm64 -E $file
 *
 * check-output-start

1
__x86_64__
1
1234
4
8
unsigned long
 * check-output-end
 */
