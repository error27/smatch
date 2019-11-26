#if defined(__ARM_PCS_VFP) || !defined(__SOFTFP__) || !defined(__ARM_PCS)
#error
#endif

/*
 * check-name: arm-predef-soft-float
 * check-command: sparse --arch=arm -msoft-float $file
 */
