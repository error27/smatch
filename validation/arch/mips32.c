__mips__
__mips
__mips64__
__i386__
__x86_64__
__LP64__
__BYTE_ORDER__
__SIZEOF_INT__
__SIZEOF_LONG__
__SIZE_TYPE__

/*
 * check-name: arch/mips32
 * check-command: sparse --arch=mips32 -E $file
 *
 * check-output-start

1
32
__mips64__
__i386__
__x86_64__
__LP64__
4321
4
4
unsigned int
 * check-output-end
 */
