__arm__
__aarch64__
__i386__
__x86_64__
__LP64__
__BYTE_ORDER__
__SIZEOF_INT__
__SIZEOF_LONG__
__SIZE_TYPE__

/*
 * check-name: arch/arm
 * check-command: sparse --arch=arm -E $file
 *
 * check-output-start

1
__aarch64__
__i386__
__x86_64__
__LP64__
1234
4
4
unsigned int
 * check-output-end
 */
