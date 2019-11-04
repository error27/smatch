__riscv
__riscv_xlen
__i386__
__x86_64__
__LP64__
__BYTE_ORDER__
__SIZEOF_INT__
__SIZEOF_LONG__
__SIZE_TYPE__

/*
 * check-name: arch/riscv64
 * check-command: sparse --arch=riscv64 -E $file
 *
 * check-output-start

1
64
__i386__
__x86_64__
1
1234
4
8
unsigned long
 * check-output-end
 */
