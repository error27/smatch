static int foo(void)
{
       goto rtattr_failure;
rtattr_failure: __attribute__ ((unused))
       return -1;
}
/*
 * check-name: Label attribute
 */
